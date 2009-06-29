#
# Regular cron jobs for the gst-sh-mobile package
#
0 4	* * *	root	[ -x /usr/bin/gst-sh-mobile_maintenance ] && /usr/bin/gst-sh-mobile_maintenance
