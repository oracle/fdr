/*
 * fdrd.c - Flight Data Recorder Daemon
 *
 * Copyright (c) 2019, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * The Universal Permissive License (UPL), Version 1.0
 *
 * Subject to the condition set forth below, permission is hereby granted to any
 * person obtaining a copy of this software, associated documentation and/or
 * data (collectively the "Software"), free of charge and under any and all
 * copyright rights in the Software, and any and all patent rights owned or
 * freely licensable by each licensor hereunder covering either (i) the
 * unmodified Software as contributed to or provided by such licensor, or (ii)
 * the Larger Works (as defined below), to deal in both
 *
 * (a) the Software, and
 *
 * (b) any piece of software and/or hardware listed in the lrgrwrks.txt file if
 * one is included with the Software each a "Larger Work" to which the Software
 * is contributed by such licensors),
 *
 * without restriction, including without limitation the rights to copy, create
 * derivative works of, display, perform, and distribute the Software and make,
 * use, sell, offer for sale, import, export, have made, and have sold the
 * Software and the Larger Work(s), and to sublicense the foregoing rights on
 * either these or other terms.
 *
 * This license is subject to the following condition:
 *
 * The above copyright notice and either this complete permission notice or at a
 * minimum a reference to the UPL must be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include	<stdio.h>
#include	<string.h>
#include	<unistd.h>
#include	<stdlib.h>
#include	<errno.h>
#include	<signal.h>
#include	<ftw.h>
#include	<search.h>
#include	<limits.h>
#include	<sys/fcntl.h>
#include	<sys/stat.h>
#include	<sys/statvfs.h>
#include 	<sys/wait.h>

/*
 * Notes about process management in this daemon:
 *
 * Each ftrace instance (as specified by the various config files) will
 * have a process assigned to it, it addition to the main, parent process
 * started by systemd itself.  If the instance has a saveto directive,
 * then that process will hang around reading the trace_pipe and writing
 * the data to the file.  If there is no saveto directive for that instance
 * the process will exit.
 *
 * The parent process will handle SIGTERM from systemd and attempt to
 * clean up the ftrace buffer.
 */

/*
 * Syntax of the config file(s)
 *
 * instance ftrace-instance-name [bufsize]
 * modprobe module-name
 * enable subsystem/probe-name
 * enable subsystem/all
 * disable subsystem/probe-name
 * disable subsystem/all
 * saveto filename [maxsize]
 * minfree pct
 *
 * FUTURE
 *
 * globbing of probe names
 * better handling of logrotate failures
 * eliminate modprobe directive, use modprobe.d
 * apply ordering rules to the list
 * how do I prevent data loss if the system crashes after I've harvested
 * data from the kernel, but before it is written to the saveto file?
 * There ought to be a way to add new config files (and instances) without
 * completely restarting the service.
 */
#define	BUFSIZE	256
#define	FDR_CONFIG_DIR	"/etc/fdr.d"

#define	INST_DIR	"/sys/kernel/debug/tracing/instances"

#define	INSTANCE	"instance"
#define	INSTANCE_T	0
#define	MODPROBE	"modprobe"
#define	MODPROBE_T	1
#define	ENABLE		"enable"
#define	ENABLE_T	2
#define	DISABLE		"disable"
#define	DISABLE_T	3
#define	SAVETO		"saveto"
#define	SAVETO_T	4
#define	LOGROT		"logrotate"
#define	LOGROT_T	5
#define	MINFREE		"minfree"
#define	MINFREE_T	6

#define	MINFREE_DEFAULT	5
#define	MAXSIZE_DEFAULT	INT_MAX

/* exit codes */
#define	EC_MKDIR        1
#define	EC_SYSTEM       2
#define	EC_SYNTAX       3
#define	EC_BADTYPE1     4
#define	EC_OPEN         5
#define	EC_WRITE1       6
#define	EC_OPENLOG      7
#define	EC_OPENTRACE    8
#define	EC_FSTAT        9
#define	EC_MALLOC       10
#define	EC_BADVERB      11
#define	EC_FORK         12
#define	EC_BADTYPE2     13
#define	EC_BADARGS	14
#define	EC_EXEC		15

/*
 * anchor -> instance -> instance -> anchor
 *		|		|
 *		V		V
 *		item	     item
 *		item	     item
 *		|		|
 *		V		V
 */

struct item {
	struct item	*forw;
	int		typ;
	char		verb[BUFSIZE];
	char		target[BUFSIZE];
	char		fpath[BUFSIZE];
	char		optarg[BUFSIZE];
	int		line;
};

struct instance {
	struct instance	*forw;
	struct instance *back;
	struct item	*ifirst;
	struct item	*ilast;
	char		iname[BUFSIZE];
	char		dname[BUFSIZE];	/* name of the ftrace directory */
	int		fud;
	unsigned long	bufsize;	/* ftrace buffer size, per cpu */
	long		maxsize;	/* max file size of saveto target */
	int		minfree;	/* minimum free space percentage */
};

struct {
	struct instance *forw;
	struct instance	*back;
	int	numi;
} anchor = { (struct instance *) &anchor, (struct instance *) &anchor, 0 };

int verbose;
int got_sighup;
char inst_dir[BUFSIZE];

void
sighandler(int signo, siginfo_t *sp, void *vp)
{
	struct instance *insp;

	if (verbose > 1)
		fprintf(stderr, "signal %d received, cleaning up\n", signo);

	for (insp = anchor.forw; (void *)insp != (void *)&anchor;
	     insp = insp->forw) {

		if (verbose > 1)
			fprintf(stderr, "removing %s\n", insp->dname);

		if (rmdir(insp->dname))
			fprintf(stderr, "rmdir %s failed, errno %d\n",
				insp->dname, errno);
	}
	exit(0);
}

void
sighup(int signo, siginfo_t *sp, void *vp)
{
	if (verbose > 1)
		fprintf(stderr, "SIGHUP received\n");
	/*
	 * This will cause saveto to react
	 */
	got_sighup = 1;
}

unsigned long
getvalue(char *arg)
{
	unsigned long x;
	char *ep;

	x = strtoul(arg, &ep, 0);
	if (*ep == 'k' || *ep == 'K')
		x *= 1024;
	else if (*ep == 'm' || *ep == 'M')
		x *= 1024*1024;
	else if (*ep == 'g' || *ep == 'G')
		x *= 1024*1024*1024;

	return x;
}

void
instance(struct instance *insp, struct item *itp)
{
	char bs[BUFSIZE], value[BUFSIZE];
	int fd, sl;

	fprintf(stderr, "creating: %s\n", insp->dname);
	(void) rmdir(insp->dname);
	errno = 0;
	/*
	 * If mkdir fails w/ EEXIST, then just use that, instead
	 * of just having outright failure.  It's probably a leftover
	 * from some previous instantation of the daemon that didn't
	 * get cleaned up properly.
	 */
	if (mkdir(insp->dname, 0700) && errno != EEXIST) {
		perror(insp->dname);
		exit(1);
	}
	if (insp->bufsize) {
		fprintf(stderr, "%s: bufsize %lu\n", itp->target,
			insp->bufsize);
		snprintf(bs, sizeof(bs), "%s/buffer_size_kb", insp->dname);

		fd = open(bs, O_WRONLY);
		if (fd < 0)
			perror(bs);
		else {
			snprintf(value, sizeof(value), "%lu", insp->bufsize);
			sl = strlen(value);
			if (write(fd, value, sl) == -1)
				perror(bs);
			close(fd);
		}

	}
}

void
load_module(struct item *itp)
{
	char cmdline[BUFSIZE];

	snprintf(cmdline, sizeof(cmdline), "modprobe %s", itp->target);
	if (system(cmdline)) {
		perror(cmdline);
		exit(EC_SYSTEM);
	}
}

void
enable_or_disable(struct instance *insp, struct item *itp)
{
	/*
	 * need to parse out subsys/probe from subsys/all
	 */
	char fname[BUFSIZE], *sp, *wbuf, *msg;
	int fd, len;

	if (verbose > 1 && itp->optarg[0])
		fprintf(stderr, "eod: optarg: %s\n", itp->optarg);

	sp = strchr(itp->target, '/');

	if (!sp) {
		fprintf(stderr, "missing slash on line %d in %s\n",
			itp->line, itp->fpath);
		exit(EC_SYNTAX);
	}
	sp++;
	if (strncmp(sp, "all", 3) == 0) {
		/* .../subsystem/enable */
		*--sp = '\0';
		snprintf(fname, sizeof(fname), "%s/%s/events/%s/enable",
			inst_dir, insp->iname, itp->target);
	} else {
		/* .../subsystem/probe-name */
		snprintf(fname, sizeof(fname), "%s/%s/events/%s/enable",
			inst_dir, insp->iname, itp->target);
	}

	if (itp->typ == ENABLE_T) {
		wbuf = "1";
		msg = "enable";
	} else if (itp->typ == DISABLE_T) {
		wbuf = "0";
		msg = "disable";
	} else {
		fprintf(stderr, "internal error 2, bad typ %d\n", itp->typ);
		exit(EC_BADTYPE2);
	}

	if (verbose > 1)
		fprintf(stderr, "%s: %s\n", msg, fname);

	fd = open(fname, O_WRONLY);
	if (fd < 0) {
		if (errno == ENOENT) {
			fprintf(stderr, "%s: no such probe\n", itp->target);
			/* not fatal, just return */
			return;
		}
		perror(fname);
		exit(EC_OPEN);
	}

	if (write(fd, wbuf, 1) != 1) {
		perror("write");
		exit(EC_WRITE1);
	}
	close(fd);

	/* Enable a filter if one was specified */
	if (itp->optarg[0] == '\0')
		return;

	snprintf(fname, sizeof(fname), "%s/%s/events/%s/filter",
		inst_dir, insp->iname, itp->target);

	fprintf(stderr, "applying filter '%s' to %s\n", itp->optarg, fname);

	fd = open(fname, O_WRONLY);
	if (fd < 0) {
		perror(fname);
		return;
	}
	len = strlen(itp->optarg);
	if (write(fd, itp->optarg, len) != len) {
		perror(fname);
		return;
	}
	close(fd);
}

int
openw(char *f)
{
	int fd;

	fd = open(f, O_WRONLY|O_CREAT|O_TRUNC, 0400);
	if (fd < 0) {
		perror(f);
		exit(EC_OPENLOG);
	}
	return (fd);
}

/*
 * Invoke logrotate(8) directly.  This is necessary because fdr may
 * generate so much data as to make the crontab initiated log rotations
 * to not be able to keep up.
 */
void
rotate(struct instance *insp)
{
	struct stat s;
	char cfile[BUFSIZE];
	int status;
	pid_t pid;
	char *args[4];

	/*
	 * Check to see if a logrotate(8) config file exists
	 * with this instance name in /etc/logrotate.d.  If
	 * so, then use this.  Otherwise, do it by hand.
	 */
	snprintf(cfile, sizeof(cfile), "/etc/logrotate.d/%s", insp->iname);

	if (verbose > 1)
		fprintf(stderr, "looking for %s\n", cfile);

	if (stat(cfile, &s) == 0) {
		pid = fork();
		if (pid == 0) {
			args[0] = "logrotate";
			args[1] = "-f";
			args[2] = cfile;
			args[3] = NULL;

			if (execv("/usr/sbin/logrotate", args)) {
				perror("cannot exec logrotate");
				exit(EC_EXEC);
			}
		} else if (pid == -1) {
			perror("fork");
		} else {
			if (waitpid(pid, &status, 0) == pid) {
				if (!(WIFEXITED(status) &&
				    WEXITSTATUS(status) == 0)) {
					fprintf(stderr, "logrotate failed %d\n",
					    status);
				}
			}
		}
	}
}

/*
* If the number of probes is too high for the workload, we don't want to
* flood the log with messages.  Return 1 if the message should be throttled
* and 0 if it should be emitted.
*/
int
throttle(int *counter)
{
	return (((*counter)++ % 1000) == 0) ? 0 : 1;
}

void
saveto(struct instance *insp, struct item *itp)
{
	char fname[BUFSIZE];
	int rfd, wfd, n, rsize, pctf, logoro = 0;
	char *buf;
	struct stat s;
	struct statvfs vfs;
	struct sigaction sa;
	int warn1 = 0, warn2 = 0, warn3 = 0;

	/*
	 * If the saveto file already exists, then rotate it. This avoids
	 * the rather unfortunate scenario where the system reboots and then
	 * immediately overwrites the old data.
	 */
	if (stat(itp->target, &s) == 0 && s.st_size > 0) {
		if (verbose)
			fprintf(stderr, "rotating %s\n", itp->target);
		rotate(insp);
	}

	snprintf(fname, sizeof(fname), "%s/%s/trace_pipe",
		inst_dir, insp->iname);

	fprintf(stderr, "saving from %s to %s\n", fname, itp->target);

	sa.sa_sigaction = sighup;
	sa.sa_flags = 0;
	if (sigaction(SIGHUP, &sa, NULL))
		perror("sigaction SIGHUP, continuing");

	rfd = open(fname, O_RDONLY);
	if (rfd < 0) {
		perror(fname);
		exit(EC_OPENTRACE);
	}
	if (fstat(rfd, &s)) {
		perror(fname);
		exit(EC_FSTAT);
	}
	rsize = s.st_blksize;
	buf = malloc(rsize);
	if (buf == NULL) {
		perror("malloc");
		exit(EC_MALLOC);
	}
	insp->fud = rfd;

	wfd = openw(itp->target);

	for (;;) {
		n = read(rfd, buf, rsize);

		if (n == -1 && got_sighup) {
			if (verbose > 1)
				fprintf(stderr, "got SIGHUP from read\n");
			got_sighup = 0;
			/*
			 * Most likely a SIGHUP from logrotate(8), we
			 * need to close the current file and make a
			 * new one.  That way, the name of the current
			 * log file is preserved in spite of the action
			 * of logrotate.
			 */
			(void) close(wfd);
			wfd = openw(itp->target);
			continue;
		}
		if (n == 0)
			break;

		if (fstat(wfd, &s) == 0) {
			/*
			 * If the file has been unlinked, then close the
			 * fd so that free space will be released.
			 */
			if (s.st_nlink == 0) {
				fprintf(stderr, "closing %s\n", itp->target);
				(void) close(wfd);
				wfd = openw(itp->target);

			} else if (s.st_size > insp->maxsize) {
			    if (!throttle(&warn1)) {
					fprintf(stderr, "file size for %s ",
						itp->target);
					fprintf(stderr, "exceeded, rotating\n");
				}
				close(wfd);
				rotate(insp);
				wfd = openw(itp->target);
			}
		}
		/*
		 * Be careful not to run the file system out of space,
		 * hold 5% in reserve.
		 */
		if (fstatvfs(wfd, &vfs) == 0) {
			pctf = (vfs.f_bavail * 100) / vfs.f_blocks;
			if (pctf <= insp->minfree) {
				if (!throttle(&warn2))
					fprintf(stderr,
					"free space too low for %s\n",
					itp->target);

				/* Try a log rotation. */
				if (logoro == 0) {
					close(wfd);
					rotate(insp);
					wfd = openw(itp->target);
					logoro++;
				}
			} else
				logoro = 0;

		}
		if (write(wfd, buf, n) != n)
			if (!throttle(&warn3)) {
				perror(itp->target);
			}
	}
}


int
read_config_file(const char *fpath, const struct stat *sb, int typeflag)
{
	FILE *f;
	char buf[BUFSIZE], save_buf[BUFSIZE], *bp;
	int l;
	struct item *itp;
	struct instance *insp;
	char *cp, *verbp = NULL, *targetp = NULL, *optarg = NULL;
	char *savep;

	if (typeflag != FTW_F)
		return 0;

	cp = strstr(fpath, ".conf");
	if (cp == NULL)
		return 0;

	l = strlen(fpath);
	/* Weed out stuff like foo.conf.OLD */
	if (cp != fpath+l-strlen(".conf"))
		return 0;

	fprintf(stderr, "reading %s\n", fpath);

	f = fopen(fpath, "r");
	if (f == NULL) {
		perror(fpath);
		return 1;
	}

	insp = malloc(sizeof(struct instance));
	if (insp == NULL) {
		perror("malloc");
		return 1;
	}
	insque(insp, &anchor);
	insp->ifirst = insp->ilast = NULL;

	for (l = 1; fgets(buf, sizeof(buf), f) != NULL; l++) {

		if (buf[0] == '#' || buf[0] == '\n')
			continue;

		buf[strlen(buf)-1] = '\0';
		/*
		 * Save a copy of buf since strtok_r modifies the original.
		 */
		strncpy(save_buf, buf, sizeof(save_buf));

		/*
		 * The structure of the directives is:
		 * verb target [optional-argument]
		 */
		bp = buf;
		verbp = targetp = optarg = NULL;
		while ((cp = strtok_r(bp, " \t", &savep)) != NULL) {
			bp = NULL;	/* for next call to strtok */
			if (verbp == NULL) {
				verbp = cp;
				continue;
			} else if (targetp == NULL) {
				targetp = cp;
				continue;
			} else if (optarg == NULL) {
				optarg = &save_buf[cp - buf];

				if (verbose > 1)
					fprintf(stderr, "optarg: %s\n", optarg);
				break;
			}
		}

		if (verbp == NULL || targetp == NULL) {
			/* no verbs found, skip the line */
			continue;
		}

		itp = malloc(sizeof(struct item));
		if (itp == NULL) {
			perror("malloc");
			exit(EC_MALLOC);
		}

		(void) strncpy(itp->verb, verbp, sizeof(itp->verb));
		(void) strncpy(itp->target, targetp, sizeof(itp->target));
		/* fpath could to be stored in the instance */
		(void) strncpy(itp->fpath, fpath, sizeof(itp->fpath));
		itp->line = l;

		/*
		 * this could be table driven
		 * could move these checks inside the strtok loop
		 */
		if (strncmp(verbp, INSTANCE, sizeof(INSTANCE)-1) == 0) {
			(void) strncpy(insp->iname, targetp,
				sizeof(insp->iname));
			snprintf(insp->dname, sizeof(insp->dname), "%s/%s",
			    inst_dir, itp->target);
			itp->typ = INSTANCE_T;
			insp->bufsize = 0;	/* take the ftrace default */
			if (optarg) {
				insp->bufsize = getvalue(optarg);
				fprintf(stderr, "bufsize: %lu\n", insp->bufsize);
			}

			anchor.numi++;
		} else if (strncmp(verbp, MODPROBE, sizeof(MODPROBE)-1) == 0)
			itp->typ = MODPROBE_T;
		else if (strncmp(verbp, ENABLE, sizeof(ENABLE)-1) == 0) {
			itp->typ = ENABLE_T;
			if (optarg)
				strncpy(itp->optarg, optarg,
					sizeof(itp->optarg));
			else
				itp->optarg[0] = '\0';
		} else if (strncmp(verbp, DISABLE, sizeof(DISABLE)-1) == 0)
			itp->typ = DISABLE_T;
		else if (strncmp(verbp, SAVETO, sizeof(SAVETO)-1) == 0) {
			itp->typ = SAVETO_T;
			if (optarg)
				insp->maxsize = getvalue(optarg);
			else
				insp->maxsize = MAXSIZE_DEFAULT;

			if (verbose > 1)
				fprintf(stderr, "maxsize: %ld\n",
				    insp->maxsize);

		} else if (strncmp(verbp, MINFREE, sizeof(MINFREE)-1) == 0) {
			itp->typ = MINFREE_T;
			insp->minfree = MINFREE_DEFAULT;
			if (targetp)
				insp->minfree = atoi(targetp);

			if (insp->minfree > 100 || insp->minfree <= 0)
				insp->minfree = MINFREE_DEFAULT;

			if (verbose)
				fprintf(stderr, "minfree: %d\n",
				    insp->minfree);

		} else {
			fprintf(stderr, "BAD verb at %d in %s\n", l, fpath);
			exit(EC_BADVERB);
		}

		if (insp->ifirst == NULL) {
			/* empty, but put it on the front */
			insp->ifirst = insp->ilast = itp;
			itp->forw = NULL;
		} else {
			/* put it on the end */
			itp->forw = NULL;
			insp->ilast->forw = itp;
			insp->ilast = itp;
		}
	}
	return 0;
}

void
make_one_instance(struct instance *insp)
{
	pid_t p;
	struct item *itp;
	struct sigaction sa;

	if (verbose > 1)
		fprintf(stderr, "creating instance for %s\n", insp->iname);

	fflush(stdout);
	fflush(stderr);

	p = fork();
	if (p == (pid_t) -1) {
		perror("fork");
		exit(EC_FORK);
	}
	if (p)
		return;	/* parent just returns */

	sa.sa_handler = SIG_DFL;
	sa.sa_flags = 0;
	if (sigaction(SIGTERM, &sa, NULL) ||
	    sigaction(SIGINT, &sa, NULL))
		perror("sigaction");


	for (itp = insp->ifirst; itp != NULL; itp = itp->forw)
		switch (itp->typ) {
		case INSTANCE_T:
			instance(insp, itp);
			break;
		case MODPROBE_T:
			load_module(itp);
			break;
		case ENABLE_T:
		case DISABLE_T:
			enable_or_disable(insp, itp);
			break;
		case SAVETO_T:
			saveto(insp, itp);
			break;
		case MINFREE_T:
			/* ignore it, already set in the instance */
			break;
		default:
			fprintf(stderr, "internal error 1, bad typ %d\n",
				itp->typ);
			exit(EC_BADTYPE1);
		}

	fprintf(stderr, "instance %s exiting\n", insp->iname);
	exit(0);
}

int
main(int argc, char **argv)
{
	struct sigaction sa;
	struct instance *insp;
	int opt, ret;

	ret  = daemon(1, 1);
	if (ret)
		return ret;

	while ((opt = getopt(argc, argv, "vd:")) != -1)
		switch (opt) {
		case 'v':
			verbose++;
			break;
		case 'd':
			snprintf(inst_dir, sizeof(inst_dir), "%s/instances",
				optarg);
			break;
		default:
			fprintf(stderr, "%s: [-v] [-d instance-dir-name]\n",
				argv[0]);
			exit(EC_BADARGS);
		}

	if (inst_dir[0] == '\0')
		strncpy(inst_dir, INST_DIR, sizeof(inst_dir));

	sa.sa_sigaction = sighandler;
	sa.sa_flags = 0;
	if (sigaction(SIGTERM, &sa, NULL) ||
	    sigaction(SIGINT, &sa, NULL))
		perror("sigaction");	/* continue anyway */

	/*
	 * Ignore SIGHUP because logrotate will send it when the log
	 * files are rotated.  The procs handling saveto() will deal
	 * with it.
	 */
	sa.sa_handler = SIG_IGN;
	sa.sa_flags = 0;
	if (sigaction(SIGHUP, &sa, NULL))
		perror("sigaction SIGHUP");	/* continue anyway */

	ftw(FDR_CONFIG_DIR, read_config_file, 1);
	/*
	 * At this point, we've ingested all the config files and
	 * no syntax errors were detected.  Create a separate process
	 * for each instance.  This could be done with pthreads.
	 */
	for (insp = anchor.forw; (void *)insp != (void *)&anchor;
	     insp = insp->forw)
		make_one_instance(insp);

	/* Just hang out and let systemd terminate us via SIGTERM. */
	pause();
	exit(0);
}
