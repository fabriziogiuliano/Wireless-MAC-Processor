#!/usr/bin/env python2
'''Utilities for performing Metamac experiments.
Requires Python 2.X for Fabric.
Author: Nathan Flick
'''

import os
import os.path
import datetime

import fabric.api as fab
import fabric.utils

DEFAULT_MAC = 'tdma-4.txt'

@fab.task
@fab.runs_once
def check_all_hosts(interface):
    hosts = ['alix{0}'.format(i) for i in xrange(1, 21)]
    with fab.settings(skip_bad_hosts=True, abort_on_prompts=True):
        results = fab.execute(check_host, interface, hosts=hosts)
    print '== RESULTS =='
    for host, has_interface in results.iteritems():
        if has_interface == True:
            print '{0} has {1}'.format(host, interface)
    print '== END RESULTS =='

@fab.task
def check_host(interface):
    with fab.settings(warn_only=True):
        try:
            return fab.run('ip link show | grep {0}'.format(interface)).succeeded
        except SystemExit: # Hack to effect skip-on-prompt using abort-on-prompt setting provided by Fabric.
            return False

@fab.task
@fab.runs_once
def load_all_b43():
    hosts = ['alix{0}'.format(i) for i in xrange(1, 21)]
    with fab.settings(skip_bad_hosts=True, abort_on_prompts=True):
        fab.execute(load_b43, hosts=hosts)

@fab.task
def load_b43():
    try:
        fab.run('modprobe b43')
    except SystemExit: # Hack to effect skip-on-prompt using abort-on-prompt setting provided by Fabric.
        pass

@fab.task
@fab.parallel
def setup(branch='metamac', get_src=True,firmware_branch=None, debug=False,gituser='fabriziogiuliano'):

    '''Sets up the node by downloading the specified branch or commit and extracting necessary
    files, installing the WMP firmware, and building needed tools.
    '''

    if firmware_branch is None:
        firmware_branch = branch
    if get_src:
	    with fab.cd('/tmp'):
		fab.run('rm -f *.deb')
		fab.run('wget http://security.ubuntu.com/ubuntu/pool/main/libx/libxml2/libxml2_2.9.1+dfsg1-3ubuntu4.7_i386.deb')
		fab.run('wget http://security.ubuntu.com/ubuntu/pool/main/libx/libxml2/libxml2-dev_2.9.1+dfsg1-3ubuntu4.7_i386.deb')
		fab.run('dpkg -i *.deb')
		fab.run('rm -f *.deb')

	    fab.run('rm -rf metamac && mkdir metamac')
	    with fab.cd('metamac'):
		fab.run('wget github.com/{1}/wireless-mac-processor/archive/{0}.zip'.format(branch,gituser))
		fab.run('unzip {0}.zip "wireless-mac-processor-{0}/wmp-injection/bytecode-manager/*"'.format(branch))
		fab.run('unzip {0}.zip "wireless-mac-processor-{0}/mac-programs/metaMAC-program/*"'.format(branch))
	
		if firmware_branch != branch:
		    fab.run('rm {0}.zip'.format(branch))
		    fab.run('wget github.com/{1}/wireless-mac-processor/archive/{0}.zip'.format(firmware_branch,gituser))

		fab.run('unzip {0}.zip "wireless-mac-processor-{0}/wmp-engine/broadcom-metaMAC/*"'.format(firmware_branch))
		fab.run('rm {0}.zip'.format(firmware_branch))

    with fab.cd('metamac/wireless-mac-processor-{0}/wmp-engine/broadcom-metaMAC/'.format(firmware_branch)):
        fab.run('cp ucode5.fw b0g0bsinitvals5.fw b0g0initvals5.fw /lib/firmware/b43')
        with fab.settings(warn_only=True):
            fab.run('rmmod b43')
        fab.run('sleep 1')
        fab.run('modprobe b43 qos=0')
        fab.run('sleep 1')
    with fab.cd('~/metamac/wireless-mac-processor-{0}/wmp-injection/bytecode-manager/'.format(branch)):
        if debug:
            fab.run("sed -i 's/CFLAGS=/CFLAGS=-g /' Makefile")
            fab.run("sed -i 's/-O[0-9]/ /' Makefile")
        fab.run('make bytecode-manager')
        fab.run('make metamac')
        fab.run('make tsfrecorder')
        fab.run('cp -f bytecode-manager ~/metamac/')
        fab.run('cp -f metamac ~/metamac/')
        fab.run('cp -f tsfrecorder ~/metamac/')
@fab.task
@fab.parallel
def setup_local(branch='metamac', get_src=True,firmware_branch=None, debug=False):

    '''Sets up the node by downloading the specified branch or commit and extracting necessary
    files, installing the WMP firmware, and building needed tools.
    '''

    if firmware_branch is None:
        firmware_branch = branch
    if get_src:
	    with fab.cd('/tmp'):
		fab.run('rm -f *.deb')
		fab.run('wget http://security.ubuntu.com/ubuntu/pool/main/libx/libxml2/libxml2_2.9.1+dfsg1-3ubuntu4.7_i386.deb')
		fab.run('wget http://security.ubuntu.com/ubuntu/pool/main/libx/libxml2/libxml2-dev_2.9.1+dfsg1-3ubuntu4.7_i386.deb')
		fab.run('dpkg -i *.deb')
		fab.run('rm -f *.deb')

	    fab.run('rm -rf metamac && mkdir metamac')
	    with fab.cd('metamac'):
		wmp_dir='wireless-mac-processor-{0}/'.format(branch);
	    	fab.run('mkdir {0} && mkdir {0}/wmp-injection'.format(wmp_dir));
		fab.put(local_path='git/wireless-mac-processor/wmp-injection/*',remote_path='wireless-mac-processor-{0}/wmp-injection/'.format(branch));
		
	    	fab.run('mkdir {0}/mac-programs && mkdir {0}/mac-prorams/metaMAC-program/'.format(wmp_dir));
		fab.put(local_path='git/wireless-mac-processor/mac-programs/metaMAC-program/*',remote_path='wireless-mac-processor-{0}/mac-programs/metaMAC-program/'.format(branch) );


    fab.put(local_path='git/meta-MAC/wmp-firmware/wmp3-2.28/*.fw',remote_path='/lib/firmware/b43/');
    with fab.settings(warn_only=True):
    	fab.run('rmmod b43')
    fab.run('sleep 1')
    fab.run('modprobe b43 qos=0')
    fab.run('sleep 1')

    with fab.cd('~/metamac/wireless-mac-processor-{0}/wmp-injection/bytecode-manager/'.format(branch)):
        if debug:
            fab.run("sed -i 's/CFLAGS=/CFLAGS=-g /' Makefile")
            fab.run("sed -i 's/-O[0-9]/ /' Makefile")
        fab.run('make bytecode-manager')
        fab.run('make metamac')
        fab.run('make tsfrecorder')
        fab.run('cp -f bytecode-manager ~/metamac/')
        fab.run('cp -f metamac ~/metamac/')
        fab.run('cp -f tsfrecorder ~/metamac/')

@fab.task
@fab.parallel
def reboot():
    fab.reboot()

def ap_ify(path):
    root, ext = os.path.splitext(path)
    return root + '.ap' + ext

def on_node(node):
    return fab.env.host_string.split('@')[-1] == node

@fab.task
def load_mac(mac, ap_node=None):
    '''Loads the given MAC protocol onto the WMP firmware. Paths are relative to the
    mac-programs/metaMAC-program directory
    '''

    if on_node(ap_node):
        mac = ap_ify(mac)
    fab.run('metamac/bytecode-manager -l 1 -m ~/metamac/wireless-mac-processor-*/mac-programs/metaMAC-program/{0}'.format(mac))
    fab.run('metamac/bytecode-manager -a 1')

@fab.task
def start_ap(mac):
    #fab.run('ifconfig wlan0 192.168.0.$(hostname | grep -Eo [0-9]+) netmask 255.255.255.0 up')
    #load_mac(mac)
    with fab.settings(warn_only=True):
        fab.run('killall -9 hostapd')
    	fab.run('killall -9 metamac')
    fab.run('scp ~/work/openfwwf-5.2/*.fw /lib/firmware/b43/')
    with fab.cd('work/association'):
        fab.run('if ! grep basic_rates=60 hostapd.conf; then echo "basic_rates=60" >> hostapd.conf; fi')
        fab.run('if ! grep supported_rates=60 hostapd.conf; then echo "supported_rates=60" >> hostapd.conf; fi')
        fab.run("sed -i 's/macaddr_acl=1/macaddr_acl=0/' hostapd.conf")
        fab.run("sed -i 's/channel=11/channel=6/' hostapd.conf")
    # Must not be prefixed with cd work/association or else the cd will interfere with nohup.
    fab.run('nohup work/association/up-hostapd.sh work/association/hostapd.conf 192.168.0.$(hostname | grep -Eo [0-9]+) >& hostapd.log < /dev/null &', pty=False)
    fab.run('sleep 4')
    #load_mac(mac)

@fab.task
@fab.parallel
def associate(mac=DEFAULT_MAC):

    #load_mac(mac)
    fab.run('modprobe b43 qos=0')
    fab.run('rmmod b43')
    fab.run('modprobe b43 qos=0')
    fab.run('ifconfig wlan0 192.168.0.$(hostname | grep -Eo [0-9]+) netmask 255.255.255.0')
    fab.run('iwconfig wlan0 essid alix-ap')
    fab.run('iwconfig wlan0 txpower 20dbm')
    result = fab.run('iwconfig wlan0 | grep Access | awk \'{ print $4 }\';')
    attempts = 0
    while 'Not-Associated' in result and attempts < 10:
        fab.run('iwconfig wlan0 essid alix-ap')
        fab.run('sleep 1')
        result = fab.run('iwconfig wlan0 | grep Access | awk \'{ print $4 }\';')
        attempts += 1
    if attempts >= 10:
        fabric.utils.abort('Could not associate node {0} with access point.'.format(fab.env.host_string))
    #load_mac(mac)

@fab.task
@fab.runs_once
def network(ap_node, mac=DEFAULT_MAC):

    fab.execute(kill_metamac)
    fab.execute(start_ap, ap_ify(mac), hosts=[h for h in fab.env.hosts if h.split('@')[-1] == ap_node])
    fab.execute(associate, mac, hosts=[h for h in fab.env.hosts if h.split('@')[-1] != ap_node])

@fab.task
def start_iperf_server():
    with fab.settings(warn_only=True):
        fab.run('killall iperf')
    fab.local('sleep 3')
    fab.run('nohup iperf -i 1 -s -u -y C > iperf.out 2> iperf.err < /dev/null &', pty=False)

@fab.task
def stop_iperf_server():
    with fab.settings(warn_only=True):
        fab.run('killall iperf')

@fab.task
@fab.parallel
def stop_iperf():
    with fab.settings(warn_only=True):
        fab.run('killall -9 iperf')

@fab.task
@fab.parallel
def stop_hostapd():
    with fab.settings(warn_only=True):
        fab.run('killall -9 hostapd')

@fab.task
@fab.parallel
def stop_wifi():
    with fab.settings(warn_only=True):
        fab.run('rmmod b43')

@fab.task
@fab.parallel
def run_iperf_client(server, duration):
    fab.run('iperf -c 192.168.0.$(echo {0} | grep -Eo [0-9]+) -u -t {1} -b 6000000'.format(server, duration))

@fab.task
def start_metamac(suite, ap_node=None, cycle=False):
    if on_node(ap_node):
        suite = ap_ify(suite)
    if not on_node(ap_node):
    	fab.run('killall -9 metamac; nohup metamac/metamac {0} -l metamac.log metamac/wireless-mac-processor-*/mac-programs/metaMAC-program/{1} > metamac.out 2> metamac.err < /dev/null &'.format('-c' if cycle else '', suite), pty=False)
    fab.run('sleep 2')

@fab.task
@fab.parallel
def kill_metamac():
    with fab.settings(warn_only=True):
    	fab.run('killall -9 metamac')
@fab.task
@fab.parallel
def stop_metamac(trialnum):
    fab.run('sleep 2')
    with fab.settings(warn_only=True):
    	fab.run('killall -9 metamac')
    fab.local('sleep 2')
    localname = 'data/{0}-{1}-{2}.csv'.format(datetime.date.today(), fab.env.host_string.split('@')[-1], trialnum)
    fab.local('mkdir -p data')
    with fab.settings(warn_only=True):
        fab.get(remote_path='metamac.log', local_path=localname)

@fab.task
@fab.runs_once
def pkt_dump(trialnum,action='run'):
	fab.local('sudo tcpdump -i wlan0 > /dev/null &')
	fab.local('sleep 1')
	fab.local('sudo killall -9 tcpdump')
	fab.local('sudo rmmod ath9k')
	if action=='run':
		fab.local('sudo modprobe ath9k')
		fab.local('sudo rfkill unblock all')
		fab.local('sudo ifconfig wlan0 down; sudo iwconfig wlan0 mode monitor; sudo ifconfig wlan0 up; sudo iwconfig wlan0 channel 6; sudo iwconfig wlan0 rate 6M fixed')
		localname = 'data/{0}-pkt_dump-{1}.csv'.format(datetime.date.today(), trialnum)
		fab.local('mkdir -p data')
		fab.local('sleep 5')
		#fab.local('sudo tcpdump -i wlan0 net 192.168.0.0/24 and dst port 5001 | awk \'{print $2","$14}\' | sed \'s/us//\' > '+localname+'&')
		fab.local('sudo tcpdump -i wlan0 | grep -v Ack | grep -v Beacon | grep \"IP 192.168.0\" | awk \'{print $1","$2","$14}\' | sed \'s/us//\' > '+localname+' &')



@fab.task
@fab.runs_once
def run_trial(trialnum, suite, ap_node):
    fab.execute(start_iperf_server, hosts=[ap_node])
    fab.execute(kill_metamac)
    fab.execute(start_metamac, suite, ap_node)
    fab.execute(pkt_dump,trialnum)
    fab.execute(run_iperf_client, ap_node, 60, hosts=[h for h in fab.env.hosts if h.split('@')[-1] != ap_node])
    fab.execute(stop_metamac, trialnum)
    fab.execute(stop_iperf_server, hosts=[ap_node])



@fab.task
def sync_date():
    controller_time=fab.local('echo $(date +%T)',capture=True)
    #fab.local('echo {0}'.format(controller_time))
    fab.run('date +%T -s {0}'.format(controller_time))

@fab.task
@fab.runs_once
def stop_all():
    fab.execute(stop_wifi)
    fab.execute(kill_metamac)
    fab.execute(stop_iperf)
    fab.execute(stop_hostapd)

@fab.task
def trial_errors():
    fab.run('cat metamac.err')

