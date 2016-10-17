#!/usr/bin/python
#
# Tests the addDigits plugin.
# Written by Broulaye Doumbia and Peter Maurer.
#
import sys, imp, atexit, os
sys.path.append("/home/courses/cs3214/software/pexpect-dpty/");
import pexpect, shellio, signal, time, os, re, proc_check

# Determine the path this file is in
thisdir = os.path.dirname(os.path.realpath(__file__))

#Ensure the shell process is terminated
def force_shell_termination(shell_process):
    c.close(force=True)

# pulling in the regular expression and other definitions
# this should be the eshoutput.py file of the hosting shell, see usage above
definitions_scriptname = sys.argv[1]
def_module = imp.load_source('', definitions_scriptname)

# you can define logfile=open("log.txt", "w") in your eshoutput.py if you want logging!
logfile = None
if hasattr(def_module, 'logfile'):
    logfile = def_module.logfile

#spawn an instance of the shell, note the -p flags
c = pexpect.spawn(def_module.shell,  drainpty=True, logfile=logfile, args=['-p', thisdir])

atexit.register(force_shell_termination, shell_process=c)

# set timeout for all following 'expect*' calls to 2 seconds
c.timeout = 5

#############################################################################
# Now the real test starts!
#

#############################################################################
# Test 1: Test the command with no parameters

c.sendline("addDigits");

expectedOutput = "0";

assert c.expect(expectedOutput) == 0, "Shell did not print the correct output";

#################################################################

# Test 2: Test the command with a single argument

c.sendline("addDigits 18");

expectedOutput = "9";

assert c.expect(expectedOutput) == 0, "Shell did not add correctly";

#################################################################

# Test 3: Test the command with bad argumment

c.sendline("addDigits test");

expectedOutput = "0";

assert c.expect(expectedOutput) == 0, "Shell did not print the correct output";

shellio.success()
