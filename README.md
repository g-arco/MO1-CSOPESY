#  CSOPESY Process Multiplexer CLI Emulator

This is a command-line emulator inspired by the Linux `screen` utility and multiprocess scheduling. It simulates process execution using a customizable CPU scheduler with support for basic instruction types like `PRINT`, `ADD`, `SLEEP`, etc.

---

## Setup Instructions

### 1. Create `config.txt`

Place a file named `config.txt` in the same directory as your executable. Example content:

num-cpu 2
scheduler rr
quantum-cycles 10
batch-process-freq 30
min-ins 30
max-ins 45
delay-per-exec 1


> All values are space-separated. Supported schedulers: `fcfs`, `rr`.

---

### 2. Build the Program

Compile using a C++ compiler:

g++ -std=c++17 -pthread -o csopesy main.cpp
Or for Windows (e.g. using MSYS2):

g++ -std=c++17 -pthread -o csopesy.exe main.cpp
3. Run the Emulator

./csopesy
Available Commands
You must run initialize first before any other command (except exit).

System Initialization

initialize
Loads config.txt and sets up the scheduler.

Process Commands
screen -s <process_name>
Create a new named process and attach to its screen.


screen -r <process_name>
Re-attach to a running process screen.


screen -ls
List all running and finished processes with CPU utilization.

Scheduler Commands

scheduler-start
Starts generating dummy processes at intervals defined in config.txt.

scheduler-stop
Stops batch process generation.

Report

report-util
Saves the current process and CPU utilization status to csopesy-log.txt.
 Exit

exit
Exits the CLI emulator.

Process Screen Commands
Once inside a process screen (via screen -s or screen -r), the following commands are supported:

process-smi
View the current instruction and any printed output/logs.

exit
Return to the main menu console.

Developers
Gabrielle Mae Arco

Roger Canayon Jr.

Carl Justin De Los Reyes

Andrei Dominic Viguilla


