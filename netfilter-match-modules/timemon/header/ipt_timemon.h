/*  timemon --	An iptables extension for time monitoring/control
 *  			Can be used to efficiently monitor time usage and/or implement time quotas
 *  			Can be queried using the ipttimectl userspace library
 *  			Originally designed for use with Gargoyle router firmware (gargoyle-router.com)
 *
 *
 *  Copyright © 2017 by Michael Gray <michael.gray@lantisproject.com>
 * 
 *  This file is free software: you may copy, redistribute and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation, either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  This file is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _IPT_TIMEMON_H
#define _IPT_TIMEMON_H

/*flags -- first three don't map to parameters the rest do */
#define TIMEMON_INITIALIZED		   1
#define TIMEMON_REQUIRES_SUBNET	   2
#define TIMEMON_SUBNET		   4
#define TIMEMON_CMP			   8
#define TIMEMON_CURRENT		  16
#define TIMEMON_RESET_INTERVAL	  32
#define TIMEMON_RESET_TIME		  64
#define TIMEMON_LAST_BACKUP		 128


/* parameter defs that don't map to flag bits */
#define TIMEMON_TYPE			  70
#define TIMEMON_ID			  71
#define TIMEMON_GT			  72
#define TIMEMON_LT			  73
#define TIMEMON_MONITOR		  74
#define TIMEMON_CHECK			  75
#define TIMEMON_CHECK_NOSWAP		  76
#define TIMEMON_CHECK_SWAP		  77
#define TIMEMON_NUM_INTERVALS		  78

/* possible reset intervals */
#define TIMEMON_MINUTE		  80
#define TIMEMON_HOUR			  81
#define TIMEMON_DAY			  82
#define TIMEMON_WEEK			  83
#define TIMEMON_MONTH			  84
#define TIMEMON_NEVER			  85

/* possible monitoring types */
#define TIMEMON_COMBINED 		  90
#define TIMEMON_INDIVIDUAL_SRC	  91
#define TIMEMON_INDIVIDUAL_DST 	  92
#define TIMEMON_INDIVIDUAL_LOCAL	  93
#define TIMEMON_INDIVIDUAL_REMOTE	  94



/* socket id parameters (for userspace i/o) */
#define TIMEMON_SET 			2048
#define TIMEMON_GET 			2049

/* max id length */
#define TIMEMON_MAX_ID_LENGTH		  50

/* 4 bytes for total number of entries, 100 entries of 12 bytes each, + 1 byte indicating whether all have been dumped */
#define TIMEMON_QUERY_LENGTH		1205 
#define TIMEMON_ENTRY_LENGTH		  12

/* Duration (in seconds) for how we measure time in this module. Think of this like "resolution" */
#define TIMEMON_INTERVAL_DURATION		5


struct ipt_timemon_info
{
	char id[TIMEMON_MAX_ID_LENGTH];
	unsigned char type;
	unsigned char check_type;
	uint32_t local_subnet;
	uint32_t local_subnet_mask;

	unsigned char cmp;
	unsigned char reset_is_constant_interval;
	time_t reset_interval; //specific fixed type (see above) or interval length in seconds
	time_t reset_time; //seconds from start of month/week/day/hour/minute to do reset, or start point of interval if it is a constant interval
	uint64_t timeusage_cutoff;
	uint64_t current_timeusage;
	time_t next_reset;
	time_t previous_reset;
	time_t last_record_time;
	time_t last_backup_time;

	unsigned long hashed_id;
	void* iam;
	uint64_t* combined_tm;
	struct ipt_timemon_info* non_const_self;
	unsigned long* ref_count;


};
#endif /*_IPT_TIMEMON_H*/