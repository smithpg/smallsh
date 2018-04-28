#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <string.h>
#include <fcntl.h>

#include <errno.h>

#include "dynamicArray.h"

struct shell {
	int status;
	short foregroundOnly;
	short foregroundEnterMessage;
	short foregroundExitMessage;

	char messageBuffer[50];

	pid_t foregroundPid;
	
	DynamicArray* backgroundPids;
        DynamicArrayIterator* backgroundIter;	
};


// global shell struct holds status of last terminated command, and the array of active pids
struct shell shell;

struct command {
	char* argv[513];
	char outputRedirect[255];
	char inputRedirect[255];
	short backgrounded; 
};


void commandFreeArgv(struct command* command){

	int i = 0;
	while (command->argv[i] != NULL){
		free(command->argv[i]);
		i++;
	}

}

void handleSIGINT(int signo){
	
	fflush(stdout);	
	
	if (shell.foregroundPid != -1){
		// Terminate the foreground process, but not shell itself
		kill(shell.foregroundPid, SIGTERM);	
		waitpid(shell.foregroundPid, &shell.status, NULL);
	}

	else {
		
	}


}

void handleSIGTSTP(int signo){

	// Toggle foreground-only mode
	if (shell.foregroundOnly == 0){
		shell.foregroundOnly = 1;
		shell.foregroundEnterMessage = 1;
	}

	else{
		shell.foregroundOnly = 0;
		shell.foregroundExitMessage = 1;
	}
}

char* statusMessage(char* buffer){

	// Clear the buffer
	short i = 0;
	while (buffer[i] != '\0'){
		buffer[i] = '\0';
		i++;
	}
	
	// Write new status message to it. 
	if (WIFEXITED(shell.status)){
		sprintf(buffer, "exit code %i", WEXITSTATUS(shell.status));
	}
	else{
		sprintf(buffer, "terminating signal %i", WTERMSIG(shell.status));
	}

	return buffer;
}



void checkBackground(){
	shell.backgroundIter = dyIteratorNew(shell.backgroundPids);
	while(dyIteratorHasNext(shell.backgroundIter)){
		pid_t returnPid = waitpid(dyIteratorNext(shell.backgroundIter), &shell.status, WNOHANG);	
		if (returnPid){
			// if it terminated, remove from array
			dyIteratorRemove(shell.backgroundIter);
			
			// and display a message.
			printf("background pid %i is done : %s\n", returnPid, statusMessage(shell.messageBuffer));
			fflush(stdout);
		}
			
	}
	dyIteratorDelete(shell.backgroundIter);
}


char* replaceChars(const char* original, const char* target, const char* replacement){
	
	// If the target can't be found, return the original char*
	if (strstr(original, target) == NULL) return original;



	short origLen = strlen(original);
	short targetLen = strlen(target);
	short replacementLen = strlen(replacement);

	short targetStart = strstr(original, target) - original; 

	// Build new string
	char* newString = malloc(origLen - targetLen + replacementLen); 		
	

	int i;
	while (i < targetStart){
		newString[i] = original[i];
		i++;
	}
	
	while (i < targetStart + replacementLen){
		newString[i] = replacement[i - targetStart];
		i++;
	}

	while (i < (origLen - targetLen + replacementLen)){
		newString[i] = original[i - replacementLen + targetLen];
		i++;
	}

	newString[i] = '\0';

	return newString;
}	

void prompt(){

	// Report change to foreground-only mode, if occurred
			
	if (shell.foregroundEnterMessage == 1){
		shell.foregroundEnterMessage = 0;
		printf("Entering foreground-only mode (& is now ignored)\n");
	}

	else if (shell.foregroundExitMessage == 1){
		shell.foregroundExitMessage = 0;
		printf("Exiting foreground-only mode\n");
	}
	
	// Print the prompt symbol
	printf("\n:");
	fflush(stdout);
}

int processInput(char* inputBuffer, struct command* commandStruct){
	
	commandFreeArgv(commandStruct);	
	memset(commandStruct, 0, sizeof(struct command));
	
	char* token;
	size_t bytesRead;
	int numArgs;
	
	int getlineReturnVal;
	while(1){
		// Get input, retry if signal interrupts
		getlineReturnVal = getline(&inputBuffer, &bytesRead, stdin);

		if (getlineReturnVal == -1){
			clearerr(stdin);
			prompt();
		}

		else break;
	}	

	// If first call to strtok returns NULL, input was a blank line
	token = strtok(inputBuffer, " \n");
	if (token == NULL){
		return 0;
	}

	int index = 0;
	char* argument;

	char pidString[8];
	sprintf(pidString, "%i", getpid());

	while(1) {
		// Copy args into heap memory so that 
		// string len can be extended in cases
		// where arg contains $$ expansion...


		if(strstr(token, "$$")){

			argument = replaceChars(token, "$$", pidString);
		}	
		
		else{
			argument = malloc(strlen(token));
			strcpy(argument, token);			
		}

		commandStruct->argv[index] = argument;

		token = strtok(NULL, " \n");
		if (token == NULL) break;

		index++;
	}

	// Starting from the end of the array, remove &'s and redirection 
	// operators if they exist and update the command struct to reflect
	// the indicated backgrounding / redirection.
	if (index > 0 && ((strcmp(commandStruct->argv[index],"&\0")) == 0)){
		// The command is backgrounded
		commandStruct->backgrounded = 1;
		free(commandStruct->argv[index]);
		commandStruct->argv[index] = NULL;
		index--;
	}

	while (index >= 1){
	
		if ((*(commandStruct->argv[index - 1]) == '<') && (*(commandStruct->inputRedirect) == 0)){
			
			strcpy(commandStruct->inputRedirect, commandStruct->argv[index]);
			
			// Remove the operator and filename from the args list 
			
			free(commandStruct->argv[index]);
			commandStruct->argv[index] = NULL;
			
			free(commandStruct->argv[index - 1]);
			commandStruct->argv[index - 1] = NULL;
			// Continue, skipping over the redirection operator we just addressed
			index = index - 2;
		}
					
		else if  (*(commandStruct->argv[index - 1]) == '>' && (*(commandStruct->outputRedirect) == 0)){
		
			strcpy(commandStruct->outputRedirect, commandStruct->argv[index]);	

			// Remove the operator and filename from the args list	
			free(commandStruct->argv[index]);
			commandStruct->argv[index] = NULL;
			
			free(commandStruct->argv[index - 1]);
			commandStruct->argv[index - 1] = NULL;
			// Continue, skipping over the redirection operator we just addressed
			index = index - 2; 		
		}

		else{
			// If we don't find either redirection operator, everything else is an argument
			// so, we're done.  
			break;
		}
	}	
	
	return 0;
}

void printCommand(struct command* command){

	// print the arguments
	int argIndex = 0;
	while(command->argv[argIndex] != NULL){
		printf("argv[%i]: %s\n", argIndex, command->argv[argIndex]);
		argIndex++;
	}
	if (command->outputRedirect){
		printf("Output Redirected to %s\n", command->outputRedirect);
	}

	if (command->inputRedirect){
		printf("Input redirected to %s\n", command->inputRedirect);
	}

	printf("Backgrounded = %d\n", command->backgrounded);

}



int main() {

	// Redirect stderr to stdout
	dup2(1, 2);


	struct command currentCommand = {0};
	char* lineInput = NULL;
	
	pid_t spawnpid;

	int inputFile;
	int outputFile;
	
	// Initialize shell struct
	shell.status = 0;
	shell.foregroundOnly = 0;

	shell.backgroundPids = dyNew(10);
	shell.foregroundPid = -1; // This value indicates there is no foreground
				 // process currently.

	memset(shell.messageBuffer, 0, 50);

	// Register signal handling functions
	
	struct sigaction SIGINT_action = {0}, SIGTSTP_action = {0};

	SIGINT_action.sa_handler = handleSIGINT;
	sigfillset(&SIGINT_action.sa_mask);
	SIGINT_action.sa_flags = 0;

	sigaction(SIGINT, &SIGINT_action, NULL); 

	SIGTSTP_action.sa_handler = handleSIGTSTP;
	sigfillset(&SIGTSTP_action.sa_mask);
	SIGTSTP_action.sa_flags = 0;

	sigaction(SIGTSTP, &SIGTSTP_action, NULL); 

	
	// ===== MAIN PROGRAM LOOP =====


	while(1){
			
		// Check for background process termination
		checkBackground();
		
		// Print any messages, then the ':' prompt
		prompt();

		// Get input, parse it, and store in currentCommand
		processInput(lineInput, &currentCommand);
			
		// Continue to next iteration if line is blank or a comment
		if (currentCommand.argv[0] == NULL || currentCommand.argv[0][0] == '#') continue;


		// Is it a built in command? If so no exec() is needed ... 

		// --- BUILT-IN: exit
		if (strcmp(currentCommand.argv[0], "exit") == 0){
			
			// Take care of processes running in the background ...
			shell.backgroundIter = dyIteratorNew(shell.backgroundPids);
			while(dyIteratorHasNext(shell.backgroundIter)){
				
				kill(dyIteratorNext(shell.backgroundIter), SIGTERM);
			
			}
			dyIteratorDelete(shell.backgroundIter);

			// Then exit normally.
			exit(0);
		}	
				
		// --- BUILT-IN: status
		else if (strcmp(currentCommand.argv[0], "status") == 0){
			printf("%s\n", statusMessage(shell.messageBuffer));
		}

		// --- BUILT-IN: cd
		else if (strcmp(currentCommand.argv[0], "cd") == 0){
			
			if (currentCommand.argv[1] == NULL){
				chdir(getenv("HOME"));
			}
			
			else{
				chdir(currentCommand.argv[1]);
			}
	
		}

		// If it's not a built in, we need to fork and call an exec() function.
		else{
				
			spawnpid = fork();
		
			switch(spawnpid){
			
			case -1:
				printf("fork() returned -1");
				exit(3);			
			case 0:
				// If the command is backgrounded, preemptively redirect to /dev/null
				if (currentCommand.backgrounded && (!shell.foregroundOnly)){
					int devNull = open("/dev/null", O_RDWR);

					if (dup2(devNull, 0) == -1) {
						exit(2);
					}

					
					if (dup2(devNull, 1) == -1) {
						exit(2);
					}
				}
					
				// Perform necessary redirection; /dev/null redirect will be overwritten
				// in cases where a background command contains explicit redirection.
					
				if (*(currentCommand.inputRedirect) != 0){
					inputFile = open(currentCommand.inputRedirect, O_RDONLY);			
					
					if (inputFile == -1){
						perror("Error");
						exit(2);
					}
					
					if (dup2(inputFile, 0) == -1) {
						perror("dup2 on line 176 failed");
						exit(2);
					}
				}	

				if (*(currentCommand.outputRedirect) != 0){
					
					outputFile = open(currentCommand.outputRedirect, O_WRONLY | O_CREAT | O_TRUNC, 0777);			
					
					if (outputFile == -1){
						perror("Error");
						exit(2);
					}

					if (dup2(outputFile, 1) == -1) { 						
						perror("dup2 on line 184 failed");
						exit(2);
					}
				}	
					
				// Call exec to replace process w/ appropriate program
				execvp(currentCommand.argv[0], currentCommand.argv);
					
				// If this code gets executed, it means the exec call failed 
				perror("Error");
				exit(1);	


			default:
								
				

				// For background processes ...
				if (currentCommand.backgrounded && (!shell.foregroundOnly)){
					printf("background pid is %i\n", spawnpid);
					fflush(stdout);
					
					dyAdd(shell.backgroundPids, spawnpid);	
				}		

				// For foreground processes ...
				else{	
					shell.foregroundPid = spawnpid;
					waitpid(shell.foregroundPid, &shell.status, NULL);
					shell.foregroundPid = -1; //currently no foreground process
				}
			}
		}
		
		free(lineInput);
		lineInput = NULL;
	}		
	return 0;
}
