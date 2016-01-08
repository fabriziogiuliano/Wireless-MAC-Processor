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
def setup(branch='metamac', firmware_branch=None, debug=False):
    '''Sets up the node by downloading the specified branch or commit and extracting necessary
    files, installing the WMP firmware, and building needed tools.
    '''

    if firmware_branch is None:
        firmware_branch = branch

    with fab.cd('/tmp'):
        fab.run('rm -f *.deb')
        fab.run('wget http://security.ubuntu.com/ubuntu/pool/main/libx/libxml2/libxml2_2.9.1+dfsg1-3ubuntu4.6_i386.deb')
        fab.run('wget http://security.ubuntu.com/ubuntu/pool/main/libx/libxml2/libxml2-dev_2.9.1+dfsg1-3ubuntu4.6_i386.deb')
        fab.run('dpkg -i *.deb')
        fab.run('rm -f *.deb')

    fab.run('rm -rf metamac && mkdir metamac')
    with fab.cd('metamac'):
        fab.run('wget github.com/nflick/wireless-mac-processor/archive/{0}.zip'.format(branch))
        fab.run('unzip {0}.zip "wireless-mac-processor-{0}/wmp-injection/bytecode-manager/*"'.format(branch))
        fab.run('unzip {0}.zip "wireless-mac-processor-{0}/mac-programs/metaMAC-program/*"'.format(branch))

        if firmware_branch != branch:
            fab.run('rm {0}.zip'.format(branch))
            fab.run('wget github.com/nflick/wireless-mac-processor/archive/{0}.zip'.format(firmware_branch))

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
        fab.run('cp bytecode-manager ~/metamac/')
        fab.run('cp metamac ~/metamac/')
        fab.run('cp tsfrecorder ~/metamac/')

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
    fab.run('ifconfig wlan0 192.168.0.$(hostname | grep -Eo [0-9]+) netmask 255.255.255.0 up')
    load_mac(mac)
    with fab.settings(warn_only=True):
        fab.run('killall hostapd')
    fab.run('sleep 3')
    with fab.cd('work/association'):
        fab.run('if ! grep basic_rates=60 hostapd.conf; then echo "basic_rates=60" >> hostapd.conf; fi')
        fab.run('if ! grep supported_rates=60 hostapd.conf; then echo "supported_rates=60" >> hostapd.conf; fi')
        fab.run("sed -i 's/macaddr_acl=1/macaddr_acl=0/' hostapd.conf")
    # Must not be prefixed with cd work/association or else the cd will interfere with nohup.
    fab.run('nohup work/association/up-hostapd.sh work/association/hostapd.conf 192.168.0.$(hostname | grep -Eo [0-9]+) >& hostapd.log < /dev/null &', pty=False)
    fab.run('sleep 8')
    load_mac(mac)

@fab.task
def associate(mac):
    fab.run('ifconfig wlan0 192.168.0.$(hostname | grep -Eo [0-9]+) netmask 255.255.255.0 up')
    load_mac(mac)
    with fab.cd('work/association'):
        attempts = 0
        result = fab.run('./ass.sh alix-ap 192.168.0.$(hostname | grep -Eo [0-9]+)')
        while 'iwconfig result Access' not in result and attempts < 4:
            result = fab.run('./ass.sh alix-ap 192.168.0.$(hostname | grep -Eo [0-9]+)')
            attempts += 1
        if attempts >= 4:
            fabric.utils.abort('Could not associate node {0} with access point.'.format(fab.env.host_string))
    load_mac(mac)

@fab.task
@fab.runs_once
def network(ap_node, mac=DEFAULT_MAC):
    fab.execute(start_ap, ap_ify(mac), hosts=[h for h in fab.env.hosts if h.split('@')[-1] == ap_node])
    fab.execute(associate, mac, hosts=[h for h in fab.env.hosts if h.split('@')[-1] != ap_node])

@fab.task
def start_iperf_server():
    with fab.settings(warn_only=True):
        fab.run('killall iperf')
    fab.local('sleep 3')
    fab.run('nohup iperf -s -u > iperf.out 2> iperf.err < /dev/null &', pty=False)

@fab.task
@fab.parallel
def run_iperf_client(server, duration):
    fab.run('iperf -c 192.168.0.$(echo {0} | grep -Eo [0-9]+) -u -d -t {1} -b 6000000'.format(server, duration))

@fab.task
@fab.parallel
def start_metamac(suite, ap_node=None, cycle=False):
    with fab.settings(warn_only=True):
        fab.run('killall -s INT metamac/metamac')
    fab.run('sleep 2')
    if on_node(ap_node):
        suite = ap_ify(suite)
    fab.run('nohup metamac/metamac {0} -l metamac.log metamac/wireless-mac-processor-*/mac-programs/metaMAC-program/{1} > metamac.out 2> metamac.err < /dev/null &'.format('-c' if cycle else '', suite), pty=False)
    fab.run('sleep 2')

@fab.task
@fab.parallel
def stop_metamac(trialnum):
    fab.run('sleep 2')
    with fab.settings(warn_only=True):
        fab.run('killall -s INT metamac/metamac')
    fab.local('sleep 2')
    localname = 'data/{0}-{1}-{2}.csv'.format(datetime.date.today(), fab.env.host_string.split('@')[-1], trialnum)
    fab.local('mkdir -p data')
    with fab.settings(warn_only=True):
        fab.get(remote_path='metamac.log', local_path=localname)

@fab.task
@fab.runs_once
def run_trial(trialnum, suite, ap_node):
    fab.execute(start_iperf_server, hosts=[ap_node])
    fab.execute(start_metamac, suite, ap_node)
    fab.execute(run_iperf_client, ap_node, 30, hosts=[h for h in fab.env.hosts if h.split('@')[-1] != ap_node])
    fab.execute(stop_metamac, trialnum)

@fab.task
def trial_errors():
    fab.run('cat metamac.err')
