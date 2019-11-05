# Flight Data Recorder

## Description

	The flight data recorder (fdr) is a daemon which enables ftrace probes,
	harvests ftrace data and (optionally) writes the data to a file.

	The behavior of fdr is defined by configuration files stored in
	/etc/fdr.d.  During service startup, fdr will process each file in
	the directory which has the suffix of .conf.  If new config files
	are added to the fdr.d directory, then the service must be restarted
	to recognize the new configuration information.

	fdr is controlled by systemd(8) on systems where systemd is
	available.  Error messages from fdr can be viewed via systemctl,
	for example, systemctl status -l fdr.

## Configuration File Syntax

	The following keywords and options are recognized

###	instance iname

		Create a new ftrace instance called "iname".  This instance
		will appear in /sys/kernel/debug/tracing/instances.

		The optional buffer-size parameter can be used to control
		the size of the ftrace buffers for this instance in the
		kernel.  A suffix of 'k', 'K', 'm', 'M', 'g' or 'G' may be
		used to specify kilobytes, megabytes or gigabytes.

###	modprobe module-name

		Force the named module to be loaded by fdr.  This can be
		useful when the module is normally loaded on demand and
		the probes cannot be enabled until the module is loaded.

###	enable subsystem-name/probe-name 

		Enable an ftrace probe in the specified subsystem.  Both
		the subsystem name and probe name are defined by the kernel.

		The optional filter parameter allows an ftrace filter to
		be set as well.  This will limit the amount of data being
		emitted.  The syntax of the filter language is
		defined by ftrace itself and the parameters are defined
		by the static tracepoint being enabled in the kernel.

###	enable subsystem-name/all

		Enable all ftrace probes for the subsystem.

###	disable	subsystem-name/probe-name

		Disable an ftrace probe in the specified subsystem.  This
		can be useful to disable selective probes when the "ALL"
		keyword has been used.

###	disable	subsystem-name/all

		Disable all probes in the specified subsystem.

###	saveto file-name 

		Save the output of enabled probes to the named file.  If
		the optional maxsize parameter is given, the daemon will
		initiate a log rotation, see "LOG ROTATION" below.  A suffix
		of 'k', 'K', 'm', 'M', 'g' or 'G' may be used to specify
		kilobytes, megabytes or gigabytes.

		If no saveto directive is present, then fdr will create the
		instance and enable the probes.  In this case, the data
		can be harvested manually by reading:

		/sys/kernel/debug/tracing/instances/iname/trace_pipe

		The ftrace buffers in the kernel are circular. If no
		process harvests the data, new data will overwrite old data.

###	minfree value

		Limit the output by the daemon based on free space in the
		file system for the save file.  If free space percentage is
		below the specified value, no output will be written.

		If no minfree directive is present, fdr will use 5% by
		default.

## Log Rotation

	fdr can use logrotate(8) to manage the output files.  By convention,
	/etc/logrotate.d/instance-name controls the behavior of logrotate.

	fdr will also invoke logrotate directly at startup and when reaching
	the maxsize limit for the save file.

## See Also

	[trace-cmd](https://lwn.net/Articles/410200/)

	[ftrace documentation](https://www.kernel.org/doc/Documentation/trace/ftrace.txt)

## Building & Installing

	A Makefile is provided with this repository to facilitate building
	and installing fdr.  Simply type `make` to build fdr and type
	`make install` to install it

	The Makefile depends on the C compiler (provided by the gcc rpm),
	the `install` tool (provided by the coreutils rpm) as well as
	make inself (provided by make rpm).

	The source code itself depends on standard header files such
	as <stdio.h> (provided by glibc-headers).

## License

	This repository is licensed under the "Universal Permissive
	License" (UPL).  See [LICENSE](/LICENSE) in this repository for
	more information.

## Contributing

	Contributions to this respository will require an Oracle Contributor
	Agreement, see [CONTRIBUTING](/CONTRIBUTING.md) for more information.

