/*  libipttmctl --	A userspace library for querying the timemon iptables module
 *  			Originally designed for use with Gargoyle router firmware (gargoyle-router.com)
 *
 *  Heavily lifted from libiptbwctl by Eric Bishop. Most of the credit belongs
 *  with him.
 *  Copyright © 2009 by Eric Bishop <eric@gargoyle-router.com>
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


#include <ipt_tmctl.h>
#define malloc ipt_tmctl_safe_malloc
#define strdup ipt_tmctl_safe_strdup


int main(int argc, char **argv)
{
	if(argc > 1)
	{
		unsigned long num_ips;
		ip_tm_history* histories = load_history_from_file(argv[1], &num_ips);
		if(histories != NULL)
		{
			print_histories(stdout, argv[1], histories, num_ips, 'h');
		}
	}
	return 0;
}