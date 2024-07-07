#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/wait.h>

#define MAX_PIPES 6
#define MAX_ARGS 5 
#define OUTPUT_APPEND 2 
#define OUTPUT_TRUNC 1 

pid_t last_background_pid = -1; // Initialize to -1 indicating no background process yet

int validateArgsAndSpecialChars(char** arguments, int numOfArgs);

// Function to replace the first occurrence of '~' with the user's home directory
char* replace_tilde_with_home(char* command) {
    // Find the first occurrence of '~' in the command
    char* tilde = strchr(command, '~');
    if (tilde) {
        char* home = getenv("HOME"); // Get the path to the home directory
        if (home) {
            // Allocate enough space for the new command string
            char* newCommand = malloc(strlen(command) + strlen(home) - 1); // -1 because we replace 1 character ('~')
            if (newCommand) {
                // Copy the part of the command before '~'
                strncpy(newCommand, command, tilde - command);
                // Append the home directory path
                strcpy(newCommand + (tilde - command), home);
                // Append the rest of the command after '~'
                strcat(newCommand, tilde + 1);
                // Replace the original command with the new one
                //free(command);
                return newCommand;
            } else {
                // Memory allocation failed, return the original command
                return command;
            }
        }
    }
    // If '~' was not found or HOME is not set, return the original command
    return command;
}


// This function checks if a character in a string is preceded by an odd number of backslashes, indicating escape
int isCharEscaped(const char* inputString, int position) {
    // Only start checking if we are not at the beginning of the string and the previous character is a backslash
    if (position > 0 && inputString[position - 1] == '\\') {
        int backslashCount = 0; // Counter for consecutive backslashes
        // Move backwards through the string as long as we encounter backslashes
        while (position > 0 && inputString[position - 1] == '\\') {
            backslashCount++;  // Increment backslash count
            position--;        // Move to the previous character
        }
        // If the number of consecutive backslashes is odd, the character is considered escaped
        return (backslashCount % 2) == 1;
    }
    // If not starting with a backslash or at the start of the string, it's not escaped
    return 0;
}


// Removes white spaces from the beginning and end of the string
char* trimWhitespace(char *arggs) {
    int i;
    while (isspace (*arggs)) {
        arggs++;  
    }
    for (i = strlen (arggs) - 1; (isspace (arggs[i])); i--) ;   
    arggs[i + 1] = '\0';
    
    return arggs;
}

char* safeStrdupAndTrim(char *str) {
    char* trimmed = trimWhitespace(str);
    return strdup(trimmed); // Duplicate the trimmed string
}

// This function formats a command string by adding spaces around special characters
char* formatCommandArguments(const char* originalCommand) {
    const char* specialChars = "<>|&;#";  // Define special characters
    const char* doubleSpecialChars = ">|&";  // Special characters that can be doubled
    size_t originalLength = strlen(originalCommand);
    char* newCommand = malloc(originalLength * 2 + 1); // Allocate memory for the new formatted command
    size_t indexOriginal = 0, indexNew = 0;

    // Iterate through the original command string
    while (indexOriginal < originalLength) {
        // Check if current character is special and not escaped
        if (strchr(specialChars, originalCommand[indexOriginal]) != NULL && !isCharEscaped(originalCommand, indexOriginal)) {
            
            // Check for double special characters (e.g., >>) and add spaces accordingly
            if (strchr(doubleSpecialChars, originalCommand[indexOriginal]) != NULL &&
                indexOriginal + 1 < originalLength &&
                originalCommand[indexOriginal] == originalCommand[indexOriginal + 1]) {
                newCommand[indexNew++] = ' ';
                newCommand[indexNew++] = originalCommand[indexOriginal++];
                newCommand[indexNew++] = originalCommand[indexOriginal];  // Same as previous char, hence no increment
                newCommand[indexNew++] = ' ';
                newCommand[indexNew++] = ' ';
            } else {
                // Single special character, just add space before and after
                newCommand[indexNew++] = ' ';
                newCommand[indexNew++] = originalCommand[indexOriginal];
                newCommand[indexNew++] = ' ';
            }
        } else {
            // Normal character, just copy
            newCommand[indexNew++] = originalCommand[indexOriginal];
        }
        indexOriginal++; // Move to the next character in the original command
    }

    // Terminate the new command string
    newCommand[indexNew] = '\0';
    return newCommand;
}

// Validates the count of arguments in a command against predefined limits
int validateCommandArguments(char* inputArgs) {
    char* commandCopy = strdup(inputArgs); // Create a duplicate of the input to preserve the original data
    commandCopy = trimWhitespace(commandCopy); // Remove leading and trailing spaces from the command
    char* token = strtok(commandCopy, " "); // Split the command by spaces
    int count = 0; // Initialize argument count

    // Iterate over all parts of the command
    while (token != NULL) {
        token = strtok(NULL, " "); // Move to the next argument
        count++; // Increment the argument count
    }

    //free(commandCopy); // Free the duplicated command string after use
    return count >= 1 && count <= MAX_ARGS; // Check if count is within the allowed range
}

// Splits a command string into tokens based on special control characters
int partitionCommandArguments(char *input, char **tokens) {
    int count = 0;  // Initialize token count
    size_t length = strlen(input);
    size_t start = 0;  // Starting index for each token

    for (size_t idx = 0; idx < length; idx++) {
        // Identifying control sequences like &&, ||, |, ;
        if (strncmp(input + idx, "&&", 2) == 0)
        {
            tokens[count] = malloc((idx - start + 1) * sizeof(char)); 
            strncpy(tokens[count++], input + start, idx - start);
            tokens[count++] = "&&";
            idx++;
            start = idx + 2;
        } 
        // CHECK FOR OCCURENCE FOR || IN ARGUMENT
        else if (strncmp(input + idx, "||", 2) == 0)
        {
            tokens[count] = malloc((idx - start + 1) * sizeof(char));
            strncpy(tokens[count++], input + start, idx - start);
            tokens[count++] = "||";
            idx++;
            start = idx + 2;
        }

        // CHECK FOR OCCURENCE FOR | IN ARGUMENT
        else if (input[idx] == '|')
        {
            tokens[count] = malloc((idx - start + 1) * sizeof(char));
            strncpy(tokens[count++], input + start, idx - start);
            tokens[count++] = "|";
            start = idx + 1;
        }

        // CHECK FOR OCCURENCE FOR ; IN ARGUMENT
        else if (input[idx] == ';')
        {
            tokens[count] = malloc((idx - start + 1) * sizeof(char));
            strncpy(tokens[count++], input + start, idx - start);
            tokens[count++] = ";";
            start = idx + 1;
        }

        // CHECK FOR OCCURENCE FOR # IN ARGUMENT
        else if (input[idx] == '#')
        {
            tokens[count] = malloc((idx - start + 1) * sizeof(char));
            strncpy(tokens[count++], input + start, idx - start);
            tokens[count++] = "#";
            start = idx + 1;
        }
    }

    // Capture the final token, if any, after the last special character
    // if (start < length) {
    //     tokens[count] = malloc((length - start + 1) * sizeof(char));
    //     strncpy(tokens[count], input + start, length - start);
    //     tokens[count++][length - start] = '\0'; // Null-terminate the final token
    // }
    tokens[count] = malloc((length - start + 1) * sizeof(char));
    strncpy(tokens[count++], input + start, length - start);

    return count; // Return the total number of tokens generated
}


int executeSingleCommand(char* argument, int shouldFork){
    char *arr[10];   // Stores parsed command arguments
    char* cmnd = strdup(argument);
    cmnd = trimWhitespace(cmnd); // Clean command from unnecessary spaces
    int outputMode = OUTPUT_TRUNC;  // Default output redirection mode
    int bg = 0; // Flag for background execution

    char *fileIP = NULL; // Stores name of file for input redirection
    char *fileOP = NULL; // Stores name of file for output redirection
    int i = 0;

    // Detect background process indicator & handle it
    if (cmnd[strlen(cmnd) - 1] == '&') {
        bg = 1;
        cmnd[strlen(cmnd) - 1] = '\0';  // Remove '&' from command
        cmnd = trimWhitespace(cmnd);  // Clean command again after removal
    }

    if (strcmp(cmnd, "exit") == 0) {
        exit(0);
    }

    if (strcmp(cmnd, "cd") == 0) {
        chdir(getenv("HOME"));
        return 0;
    } 
    
    else if (strstr(cmnd, "cd ") == cmnd) {
        char* dir = cmnd + 3; 
        dir = trimWhitespace(dir);
        chdir(dir);
        return 0;
    }

    // Parse the command and redirections
    char *tkn, *nxtTkn;
    tkn = strtok_r(cmnd, " ", &nxtTkn);
    while (tkn != NULL) {
        if (strcmp(tkn, "<") == 0) {
            fileIP = strtok_r(NULL, " ", &nxtTkn);
        } else if (strcmp(tkn, ">") == 0) {
            fileOP = strtok_r(NULL, " ", &nxtTkn);
            outputMode = OUTPUT_TRUNC;
        } else if (strcmp(tkn, ">>") == 0) {
            fileOP = strtok_r(NULL, " ", &nxtTkn);
            outputMode = OUTPUT_APPEND;
        } else {
            arr[i++] = tkn;
        }
        tkn = strtok_r(NULL, " ", &nxtTkn);
    }
    arr[i] = NULL; // Null-terminate the argument list

    // Handling forking if necessary
    int pid = 0;
    if (shouldFork) {
        pid = fork();
    }

    // Child process
    if (pid == 0 || !shouldFork) {
        int fd_in, fd_out;

        // Setup input redirection
        if (fileIP) {
            fd_in = open(fileIP, O_RDONLY);
            if (fd_in < 0) {
                perror("Failed to open input file");
                exit(EXIT_FAILURE);
            }
            dup2(fd_in, STDIN_FILENO);
            close(fd_in);
        }

        // Setup output redirection
        if (fileOP) {
            if (outputMode == OUTPUT_TRUNC) {
                fd_out = open(fileOP, O_WRONLY | O_CREAT | O_TRUNC, 0666);
            } else { // OUTPUT_APPEND
                fd_out = open(fileOP, O_WRONLY | O_CREAT | O_APPEND, 0666);
            }
            if (fd_out < 0) {
                perror("Failed to open output file");
                exit(EXIT_FAILURE);
            }
            dup2(fd_out, STDOUT_FILENO);
            close(fd_out);
        }

        // Execute the command
        if (execvp(arr[0], arr) < 0) {
            printf("%s", arr[0]);
            perror("Failed to execute command");
            exit(EXIT_FAILURE);
        }
    } 
    // Parent process
    else {
        if (!bg) {
            int status;
            waitpid(pid, &status, 0); // Wait for child to finish
            return status;
        } else {
            printf("Background process running with PID: %d\n", pid);
            last_background_pid = pid; // Save the background process ID
            return 0;
        }
    }

    // Only reached if shouldFork is false and no forking occurred
    return 0;
}

// This function manages the execution of piped commands
int handlePipedCommands(char** argts, int argCount) {
    int pid = fork();
    int i=0;

    // This is child process where pid = 0
    if (!pid) {
        for(  i=0; i<argCount-1; i+=2)
        {
            int pd[2];
            pipe(pd);
            if (!fork()) {
                dup2(pd[1], 1); 

                // Execute command individually
                executeSingleCommand(argts[i], 0);
                printf("ERROR!!\n");
            }
            dup2(pd[0], 0);
            close(pd[1]); // Close the write end since it's not needed anymore
        }

        // Execute command individually
        executeSingleCommand(argts[i], 0);
    } 

    // This is the parent process where pid > 0
    else if (pid > 0) {
        char* cmnd = strdup(argts[argCount - 1]);
        cmnd = trimWhitespace(cmnd);
        if (cmnd[strlen(cmnd) - 1] != '&') {
            waitpid(pid, NULL, 0);
        } else {
            printf("Background process running with PID: %d\n", pid);
        }
    }
}

// Concatenates up to 5 text files and prints the result to stdout.
void concatenateFiles(char **files, int numFiles) {
    const int BUFFER_SIZE = 1024;
    char buffer[BUFFER_SIZE];
    for (int i = 0; i < numFiles; i++) {
        FILE *fp = fopen(files[i], "r");
        if (!fp) {
            perror("Failed to open file");  // This will now print the file name causing the issue
            continue;
        }
        size_t bytesRead;
        while ((bytesRead = fread(buffer, 1, BUFFER_SIZE, fp)) > 0) {
            fwrite(buffer, 1, bytesRead, stdout);
        }
        fclose(fp);
    }
}

void executeCommandWithRedirection(char *command) {
    // Example command: "cat file1.txt >> file2.txt"
    char *args[64];
    char *outputFile = NULL;
    int fd;
    int append = 0; // Flag for append mode

    // Split the command into words
    int i = 0;
    char *token = strtok(command, " ");
    while (token != NULL) {
        if (strcmp(token, ">>") == 0) {
            append = 1; // Set append flag
            token = strtok(NULL, " "); // Get the next token, which should be the filename
            outputFile = token;
            break;
        } else {
            args[i++] = token;
        }
        token = strtok(NULL, " ");
    }
    args[i] = NULL; // Null-terminate the arguments array

    // Fork to execute the command
    pid_t pid = fork();
    if (pid == 0) { // Child process
        if (outputFile) {
            // Open the output file with appropriate flags
            fd = open(outputFile, O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC), 0644);
            if (fd < 0) {
                perror("open");
                exit(EXIT_FAILURE);
            }
            dup2(fd, STDOUT_FILENO); // Redirect standard output to the file
            close(fd); // Close the original file descriptor
        }
        // Execute the command
        execvp(args[0], args);
        // If execvp returns, there was an error
        perror("execvp");
        exit(EXIT_FAILURE);
    } else if (pid > 0) { // Parent process
        int status;
        waitpid(pid, &status, 0); // Wait for the child process to finish
    } else {
        perror("fork"); // Fork failed
        exit(EXIT_FAILURE);
    }
}


// Manages the execution of parsed commands based on logical operators and piping

void processCommandTokens(char **tokens, int count) {

    int lastResult = 1; // Stores the result of the last executed command
    int index = 0; // Index for navigating through tokens
    int executionResult = 0; // Result of current command execution

    // Iterate through all command tokens
    while (index < count) {

        // Handle the logical OR '||'
        if (strcmp(tokens[index], "||") == 0) {
            if (!lastResult) { // Skip to next significant command if last result was failure
                index++;
                while (index < count && strcmp(tokens[index], "&&") != 0 &&
                       strcmp(tokens[index], "|") != 0 && strcmp(tokens[index], ";") != 0) {
                    index++;
                }
                if (index < count && index>0 && strcmp(tokens[index], "|") == 0) index += 2;
            } else {
                index++;
            }
        } 
        // Handle the logical AND '&&'
        else if (strcmp(tokens[index], "&&") == 0) {
            if (lastResult) { // Skip to next significant command if last result was success
                index++;
                while (index < count && strcmp(tokens[index], "||") != 0 &&
                       strcmp(tokens[index], "|") != 0 && strcmp(tokens[index], ";") != 0) {
                    index++;
                }
                if (index < count && index>0 && strcmp(tokens[index], "|") == 0) index += 2;
            } else {
                index++;
            }
        }
        // Handle the pipe '|'
        else if (strcmp(tokens[index], "|") == 0) {
            index++;
        } 
        // Handle the semicolon ';'
        else if (strcmp(tokens[index], ";") == 0) {
            index++;
        }
        // Handle the hashtag '#'
        else if (strcmp(tokens[index], "#") == 0) {
            index++;
        }

        // Manage the execution of a sequence of piped commands
        else if (index < count - 1 && strcmp(tokens[index + 1], "|") == 0) {
            int startPosition = index;
            while (index < count - 1 && strcmp(tokens[index + 1], "|") == 0) index += 2;
            handlePipedCommands(tokens + startPosition, index - startPosition + 1);
            index++;
        }
        // Execute a standalone command
        else {

            char* individualCommands[5];  // Assuming a max of 5 arguments for simplicity
            int argCount = partitionCommandArguments(tokens[index], individualCommands);  // This assumes you have a function to split commands into individual arguments; adjust as needed
            int isValid = validateArgsAndSpecialChars(individualCommands, argCount);
            if (!isValid) {
                printf("Invalid number of arguments for command: %s\n", tokens[index]);
                continue;  // Skip execution of this command due to invalid arguments
            }

            executionResult = executeSingleCommand(tokens[index], 1);
            lastResult = executionResult;
            index++;
        }
    }
}


// Validates the number of arguments and special characters in a command
int validateArgsAndSpecialChars(char** arguments, int numOfArgs) {
    // Verify the limit on the number of special characters
    if (numOfArgs > 2 * MAX_PIPES - 1) {
        printf("LIMIT EXCEEDED: Not many Special characters allowed.\n\n"); 
        return 0; // Invalid if too many special characters
    }

    // Loop to ensure each command has an acceptable number of arguments
    for (int index = 0; index < numOfArgs; index++) {
        // Skip odd indices as they represent special characters between commands
        if (index % 2 != 0) continue;

        // Check if the number of arguments for the command is within the allowed range
        int isValid = validateCommandArguments(arguments[index]);
        if (!isValid) { // Command has invalid number of arguments
            printf("ERROR: Each command must have 1 to 5 arguments.\nPlease try again.\n");
            return 0;
        }
    }
    return 1; // All commands and special characters are valid
}

void bringToForeground() {
    if (last_background_pid > 0) {
        int status;
        printf("Bringing process %d to the foreground\n", last_background_pid);
        waitpid(last_background_pid, &status, 0); // Wait for the process to finish
        last_background_pid = -1; // Reset the last background process ID
    } else {
        printf("No background process to bring to the foreground\n");
    }
}


int execute_newt_command() {
    pid_t pid = fork(); // Create a new process

    if (pid < 0) {
        // If fork fails, print an error message
        perror("Fork failed");
        return -1;
    } else if (pid == 0) {
        // Child process: Open a new terminal and run a new instance of the shell
        execlp("xterm", "xterm", "-e", "./shell24", NULL);

        // If execlp returns, there was an error
        perror("Failed to start a new terminal");
        exit(EXIT_FAILURE); // Exit if execlp fails
    } else {
        // Parent process: Return immediately, don't wait for the new terminal to close
        return 0; 
    }
}


// Entry point for the shell program
int main() {
    // Persistently accept user input
    while (1) {
        char* ipvar = malloc(4096); // Reserve space for command input
        fflush(stdout);  // Ensure stdout is flushed before printing prompt
        printf("shell24$ "); // Display the shell prompt

        fgets(ipvar, 1024, stdin); // Read the input from stdin

        ipvar = trimWhitespace(ipvar); // Trim leading and trailing spaces

        //ipvar = replace_tilde_with_home(ipvar);
        if (strchr(ipvar, '~') != NULL) {
            char *expandedInput = replace_tilde_with_home(ipvar);
            strcpy(ipvar, expandedInput);
            free(expandedInput);
        }

        // Check for empty input
        if (strlen(ipvar) == 0) {
            continue; // If input is empty, start the loop again
        }

        // Check for 'newt' command before processing other commands
        if (strcmp(ipvar, "newt") == 0) {
            execute_newt_command(); // Execute the 'newt' command
            continue; // Skip the rest of the loop and wait for new input
        }

        // Check for '#' character for file concatenation
        if (strchr(ipvar, '#') != NULL) {
            char *files[6]; // Supports up to 5 files
            int numFiles = 0;
            char *token = strtok(ipvar, "#");
            while (token != NULL) {
                token = trimWhitespace(token); // Trim the token in place, no need to duplicate
                files[numFiles++] = strdup(token); // Duplicate after trimming
                token = strtok(NULL, "#");
            }
            if(numFiles > 6) {
                printf("ERROR: Each command must have 1 to 5 arguments.\nPlease try again.\n");
                continue;
            }
            concatenateFiles(files, numFiles); // Concatenate and display files
            for (int i = 0; i < numFiles; i++) {
                free(files[i]); // Free the duplicated strings
            }
            continue; // Skip further processing for this command
        }

        // Check for '>>' character for output redirection
        if (strstr(ipvar, ">>") != NULL) {
            executeCommandWithRedirection(ipvar);
            continue;
        }

        if (strcmp(ipvar, "fg") == 0) {
            bringToForeground(); // Handle 'fg' command
            continue; // Skip the rest of the loop and wait for new input
        }


        // Continue with other command processing...
        char* formatInput = formatCommandArguments(ipvar); // Format the input command
        char *tkn[150]; // Array to hold tokens
        int tokenCount = partitionCommandArguments(formatInput, tkn); // Split formatted input into tokens

        // Validate command and argument counts
        int validIs = validateArgsAndSpecialChars(tkn, tokenCount);
        if (validIs) {
            processCommandTokens(tkn, tokenCount); // Execute the command if valid
        }

        // Free allocated memory at the end of each loop iteration
        free(ipvar);
        free(formatInput);
        // Remember to free other dynamically allocated memory as needed
    }
    return 0;
}
