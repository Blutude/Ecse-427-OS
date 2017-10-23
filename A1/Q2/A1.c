#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <time.h>
#include <signal.h>

//
// This code is given for illustration purposes. You need not include or follow this
// strictly. Feel free to writer better or bug free code. This example code block does not
// worry about deallocating memory. You need to ensure memory is allocated and deallocated
// properly so that your shell works without leaking memory.
//

// global var
pid_t mainPID;

///////// Signal Handling ///////////
static void
sigHandler(int sig) {
	if(sig == SIGINT){
		if (getpid() != mainPID) // So it doesnt kill the main process
			exit(0);
	}
}

///////// TOKENIZE INPUT ////////////
int getcmd(char *prompt, char *args[], int *background)
{
	int length, i = 0;
	char *token, *loc;
	char *line = NULL;
	size_t linecap = 0;
	printf("%s", prompt);
	length = getline(&line, &linecap, stdin);
	if (length <= 0) {
		exit(-1);
	}

	// Check if background is specified..
	if ((loc = index(line, '&')) != NULL) {
		*background = 1;
		*loc = ' ';
	} else
		*background = 0;
	
	while ((token = strsep(&line, " \t\n")) != NULL) {
		for (int j = 0; j < strlen(token); j++)
		if (token[j] <= 32)
			token[j] = '\0';
		if (strlen(token) > 0)
			args[i++] = token;
	}
	return i;
}

void initialize(char *args[]) {
	for (int i = 0; i < 20; i++) {
		args[i] = NULL;
	}
	return;
}

///////////////// CODE TAKEN FROM jobs.c PROVIDED BY TA ////////////////
// Global variables for background jobs
int commandNumber = 0;
struct node *head_job = NULL;
struct node *current_job = NULL;

struct node {
	int number; // the job number
	char *name; // the job name
	int pid; // the process id of the a specific process
	struct node *next; // when another process is called you add to the end of the linked list
};

//////////// PRINTING BACKGROUND JOBS. RUN BY TYPING 'JOBS' //////////////
void printBackgroundJobs(int onlyDoneJobs) { // the boolean (int) is there because I added the functionality that when a background job is done, it will print on the next command.
	if (head_job != NULL) {
		current_job = head_job;
		struct node *previous_job;
		while (current_job != NULL) {
			if (current_job == head_job) {
				int status;
				if (waitpid(current_job->pid, &status, WNOHANG) == current_job->pid) { // head_job is dead
					head_job = current_job->next;
					printf("Job number: [%d]   [DONE]   Command: %s\n", current_job->number, current_job->name);
					free(current_job);
					current_job = head_job;
				} else {
					if (!onlyDoneJobs) {
						printf("Job number: [%d]   Process id: %d   Command: %s\n", current_job->number, current_job->pid, current_job->name);						
					}
					previous_job = current_job;
					current_job = current_job->next;
				}
			} else { // current_job is in the middle or end of linked list (not head)
				int status;
				if (waitpid(current_job->pid, &status, WNOHANG) == current_job->pid) { // current_job is dead
					previous_job->next = current_job->next;
					printf("Job number: [%d]   [DONE]   Command: %s\n", current_job->number, current_job->name);
					free(current_job);
					current_job = previous_job->next;
				} else {
					if (!onlyDoneJobs) {
						printf("Job number: [%d]   Process id: %d   Command: %s\n", current_job->number, current_job->pid, current_job->name);						
					}
					previous_job = current_job;
					current_job = current_job->next;
				}
			}
		}
	} else {
		if (!onlyDoneJobs) {
			printf("No background processes are running \n");			
		}
	}
}

void addToJobList(char *args[], char *name, int process_pid) {

	struct node *job = malloc(sizeof(struct node));


	//If the job list is empty, create a new head
	if (head_job == NULL) {
		job->number = 1;
		job->name = name;
		job->pid = process_pid;

		//the new head is also the current node
		job->next = NULL;
		head_job = job;
		current_job = head_job;
	}

	//Otherwise create a new job node and link the current node to it
	else {
		current_job = head_job;
		while (current_job->next != NULL) {
			current_job = current_job->next;
		}
		job->number = current_job->number + 1;
		job->name = name;
		job->pid = process_pid;

		current_job->next = job;
		current_job = job;
		job->next = NULL;
	}
	printf("Job number: [%d]   Process id: %d   Command: %s\n", job->number, job->pid, job->name);
}


////////////// MAIN METHOD DEALING WITH DIFFERENT INPUTS ////////////////
int main(void)
{
	// Signal Handler
	if (signal(SIGINT, sigHandler) == SIG_ERR) {
		printf("ERROR! Could not bind the signal handler.\n");
		exit(1);
	}
	if (signal(SIGTSTP, sigHandler) == SIG_ERR) {
		printf("ERROR! Could not bind the signal handler.\n");
		exit(1);
	}

	time_t now;
	srand((unsigned int) (time(&now)));	
	mainPID = getpid();
	char *args[20];
	int bg;
	while(1) {
		bg = 0;
		initialize(args);
		int cnt = getcmd("\n>> ", args, &bg);

		if (*args) {

			if (!strcmp(args[0], "ls") || !strcmp(args[0], "cat")) { // those 2 commands are similar because they have output redirection
				pid_t  pid;
				pid = fork();
				if (pid == 0) {
					if (cnt > 1 && !strcmp(">", args[cnt-2])) { // assuming '>' is always the before to last argument
						int filefd = open(args[cnt-1], O_WRONLY|O_CREAT|O_TRUNC, 0666);
						close(1);
						dup(filefd);
						args[cnt-2] = NULL; // for execvp to only examine the command before the output redirection
						args[cnt-1] = NULL;
					}
					int w;
					w = rand() % 10;
					sleep(w);
					execvp(args[0], args);
					exit(0); // just to be safe but I read somewhere that execvp overwrites the process.
				}
				else {
					if (!bg) {
						int status;
						waitpid(pid, &status, WUNTRACED);
					} else {
						addToJobList(args, args[0], pid);
					}
					printBackgroundJobs(1); // 1 because printing only done jobs.
				}
			} else if (!strcmp("cd", args[0])) {

				int result = 0;
				if (args[1] == NULL) { // this will fetch home directory if you just input "cd" with no arguments
					char *home = getenv("HOME");
					if (home != NULL) {
						result = chdir(home);
					}
					else {
						printf("cd: No $HOME variable declared in the environment");
					}
				}
				//Otherwise go to specified directory
				else {
					result = chdir(args[1]);
				}
				if (result == -1) fprintf(stderr, "cd: %s: No such file or directory", args[1]);
				printBackgroundJobs(1); // 1 because printing only done jobs.				

			} else if (!strcmp("cp", args[0])) {
				pid_t  pid;
				pid = fork();
				if (pid == 0) {
					int w;
					w = rand() % 10;
					sleep(w);
					execvp(args[0], args);
					exit(0); // just to be safe but I read somewhere that execvp overwrites the process.
				}
				else {
					if (!bg) {
						int status;
						waitpid(pid, &status, WUNTRACED);
					} else {
						addToJobList(args, args[0], pid);
					}
					printBackgroundJobs(1); // 1 because printing only done jobs.
				}
			} else if (!strcmp("jobs", args[0])) {
				printBackgroundJobs(0); // 0 because printing all jobs (done or not).
			} else if (!strcmp("fg", args[0])) {
				if(args[1] != NULL){
					int n = atoi(args[1]);
					int foundBGJob = 0;
					current_job = head_job;
					struct node *previous_job;
					
					while (current_job != NULL) {
						if (n == current_job->number) {
							foundBGJob = 1;
							waitpid(current_job->pid, NULL, WUNTRACED);
							printf("Job number: [%d]   [DONE]   Command: %s\n", current_job->number, current_job->name);							
							if (current_job == head_job) {
								head_job = current_job->next;
								free(current_job);
								current_job = head_job;
							} else {
								previous_job->next = current_job->next;
								free(current_job);
								current_job = previous_job->next;
							}
							break;
						}
						previous_job = current_job;
						current_job = current_job->next;
					}
					if (!foundBGJob) {
						printf("There is no background job indexed at [%d].\n", n);
					}
					printBackgroundJobs(1); // 1 because printing only done jobs.
				} else {
					printf("Please specify the index of the background job.\n");
				}
			} else if (!strcmp("exit", args[0])) {
				break;
			} else { // additional feature for commands not mentioned in assignment
				pid_t  pid;
				pid = fork();
				if (pid == 0) {
					execvp(args[0], args);
					exit(0); // just to be safe but I read somewhere that execvp overwrites the process.
				}
				else {
					if (!bg) {
						int status;
						waitpid(pid, &status, WUNTRACED);
					} else {
						addToJobList(args, args[0], pid);
					}
					printBackgroundJobs(1); // 1 because printing only done jobs.
				}
			}

		}
	}
}