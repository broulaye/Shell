Student Information
-------------------------------
Peter Maurer - pdman13
Broulaye Doumbi - broulaye

How to execute the shell
-----------------------------
./esh
exit the shell by typing exit in the prompt.

Important Notes
------------------------------
Peter Maurer was in a group last semester, and we referenced the code from that submission for strategy. No code was taken directly, however some sections will look similar. 

Description of Base Functionality
---------------------------------
jobs: We used a list (provided) to store all jobs currently running. When the user calls jobs, we iterate through the list and print out the information about each job.

fg: Resumes the job with input pid, or the most recent one. This is done by iterating through the list to get the target job, or just popping the back. Then, the SIGCONT signal is passed, and we wait for the process to complete.

bg: Very similar to fg, however the process is not waited for at this time. Instead, we just send the signal and return to the prompt.

kill: Kills the job with input pid. This is done via the SIGKILL signal, after verifying that the process exists and the user is allowed to kill it.

stop: Stops the process, leaving it the background to potentially be resumed at a later time. This is done with the SGSTOP signal.

^C: Sends a SIGINT signal to the process. We catch this with a signal handler, preventing it from closing our shell and cleanly terminating the foreground process from our shell.

^Z: Sends a SIGTSTP signal, which we catch with a signal handler. We then stop the foreground process, moving it to the background and holding it in stasis so that it may be resumed later.

Description of Extended Functionality
-------------------------------------
I/O: Accomplished via pipes to files, opened for reading, writing, or appending.

Pipes: Done by connecting the processes with pipes, allowing them to send output/input.

Exclusive Access: By giving the foreground process terminal control, then letting it handle closing and returning. Once the process returned, returned terminal control to the shell.

List of Plugins Implemented
--------------------------------
	(Written by Our Team)
		addDigits
		adds the digits from the command line, outputs the sum

	(Written by Others)
		perm
        algasson+ishitag

        matrix_screensaver
        ajbarnes+sshumway

        coin
        env
        football
        kitty
        alecm95+alecn

        iswintercomming
        tellmeimpretty
        bhaanuk5+divyg

        platecounter
        converter
        conorg95+szuzzah
