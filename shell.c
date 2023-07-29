#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <ctype.h>
#include <signal.h>
#include <sys/resource.h>

#include <errno.h>

typedef struct Job
{
    int jobID;
    pid_t processID;
    pid_t processGroupID;
    char *status;
    int isBackground;
    char **arguments;
    int argSize;
    struct Job *next;
} Job;

Job *jobList = NULL;
pid_t currentPID = 0;
int currentJID = 0;

/**
 * FUNCTION DECLARATIONS
 * ------------------------------------------------
 *
 *
 *
 */
void printJobs();
Job *addJob(char **arguments, int isBackground, int shellArgSize, char *status, pid_t pid);
void removeJob(pid_t pid);
void freeJob(Job *node);

int execute(char **arguments, char *command, int isBackground, int shellArgSize);
char *parsePath(char **arguments);
int handleBuiltInFunction(char **shellArgs, int argSize);

void exitWithoutLeaks(char **shellArgs, int shellArgSize, char *line, char *command);
void garbageTruck(char **shellArgs, int shellArgSize, char *line, char *command);

/**
 * JOBS API
 * remove, add, and print
 * ------------------------------------------------
 *
 */

Job *addJob(char **arguments, int isBackground, int shellArgSize, char *status, pid_t pid)
{
    Job *newJob;
    newJob = malloc(sizeof(Job));
    newJob->jobID = 1;
    newJob->processID = pid;
    newJob->processGroupID = getpgrp();
    newJob->arguments = arguments;
    newJob->argSize = shellArgSize + 1;
    newJob->isBackground = isBackground;
    newJob->next = NULL;

    newJob->status = malloc(strlen(status) + 1);
    strcpy(newJob->status, status);

    newJob->arguments = malloc(newJob->argSize * sizeof(char *));
    for (int i = 0; i < newJob->argSize; i++)
    {
        newJob->arguments[i] = malloc(strlen(arguments[i]) + 1);
        strcpy(newJob->arguments[i], arguments[i]);
    }

    if (jobList == NULL)
    {
        jobList = newJob;
        return newJob;
    }

    Job *pointer = jobList;
    while (pointer->next != NULL)
    {
        pointer = pointer->next;
    }

    newJob->jobID = pointer->jobID + 1;
    pointer->next = newJob;

    return newJob;
}
void removeJob(pid_t pid)
{
    Job *temp = jobList;
    Job *prev;

    if (temp != NULL && temp->processID == pid)
    {
        jobList = temp->next;
        freeJob(temp);
        return;
    }

    while (temp != NULL && temp->processID != pid)
    {
        prev = temp;
        temp = temp->next;
    }
    if (temp == NULL)
    {
        return;
    }
    prev->next = temp->next;
    // TODO: free attributes too inside of the node like arguments
    freeJob(temp);
}
void printJobs()
{
    if (jobList == NULL)
        return;
    Job *pointer = jobList;
    while (pointer != NULL)
    {
        printf("[%d] %d %s ", pointer->jobID, pointer->processID, pointer->status);
        for (int i = 0; i < pointer->argSize; i++)
        {
            printf("%s ", pointer->arguments[i]);
        }
        printf("%c\n", pointer->isBackground ? '&' : '\0');
        pointer = pointer->next;
    }
}

/**
 *
 * SIGNAL HANDLERS
 * sigint, sigstp,  sigchild
 * ------------------------------------------------
 *
 *
 */

void sigint_handler(int sig)
{
    if (currentPID == 0)
        return;

    printf("\n[%d] %d terminated by signal %d\n", currentJID, currentPID, sig);
    kill(currentPID, sig);
}

void sigstp_handler(int sig)
{
    if (currentPID == 0)
        return;

    printf("\n");
    kill(currentPID, sig);
}

void sigchild_handler(int sig)
{
    pid_t pid;
    int status;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        removeJob(pid);
    }
}

/**
 * GARBAGE COLLECTION
 * ------------------------------------------------
 *
 */
void freeJob(Job *node)
{
    free(node->status);
    for (int i = 0; i < node->argSize; i++)
    {
        free(node->arguments[i]);
    }
    free(node->arguments);
    free(node);
}
void garbageTruck(char **shellArgs, int shellArgSize, char *line, char *command)
{
    for (int i = 0; i < shellArgSize; i++)
    {
        free(shellArgs[i]);
    }
    free(shellArgs);
    free(command);
}
void exitWithoutLeaks(char **shellArgs, int shellArgSize, char *line, char *command)
{
    garbageTruck(shellArgs, shellArgSize, line, command);

    Job *pointer = jobList;
    while (pointer != NULL)
    {
        if (strcmp(pointer->status, "Running") == 0)
        {
            kill(pointer->processID, SIGHUP);
            kill(pointer->processID, SIGCONT);
        }
        else if (strcmp(pointer->status, "Stopped") == 0)
        {
            kill(pointer->processID, SIGCONT);
        }
        pointer = pointer->next;
    }

    Job *temp;
    while (jobList != NULL)
    {
        temp = jobList;
        jobList = jobList->next;
        freeJob(temp);
    }
    free(jobList);
    exit(1);
}

/**
 * SHELL DRIVERS
 * ------------------------------------------------
 *
 *
 *
 */

int execute(char **arguments, char *command, int isBackground, int shellArgSize)
{
    int status;

    if (!isBackground)
    { // foreground
        currentPID = fork();
        if (currentPID == 0)
        {
            signal(SIGINT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);
            if (execve(command, arguments, NULL) == -1)
            {
                perror(command);
            }
        }
        else if (currentPID > 0)
        {
            waitpid(currentPID, &status, WUNTRACED);
            if (WIFSTOPPED(status))
            { // terminated by ctr+z. job is suspended
                addJob(arguments, isBackground, shellArgSize, "Stopped", currentPID);
            }
        }
        currentPID = 0;
    }

    else
    { // background job
        // automatically add as a job
        currentPID = fork();
        Job *job = addJob(arguments, isBackground, shellArgSize, "Running", currentPID);
        if (currentPID == 0)
        {
            if (execve(command, arguments, NULL) == -1)
            {
                // Print error
            }
        }
        else if (currentPID < 0)
        {
            // error forking
        }
        else
        {
            printf("[%d] %d\n", job->jobID, currentPID);
        }
        // dont wait if parent
    }

    // Loop through all jobs to check if any process's status's have changed
    // https://unix.stackexchange.com/questions/110911/how-to-get-the-job-id
    // Could use pgid values to implement job id's
    return 1;
}

// returns a path to an executable file. can also return NULL if command is invalid. handle that
char *parsePath(char **arguments)
{
    char *command = arguments[0];

    int isPath = strchr(command, '/') != NULL;

    if (isPath == 1)
    { // true
        if (access(command, X_OK) == -1)
        {
            printf("%s: No such file or directory\n", command);
            return NULL;
        }
        else
        {
            char *result = malloc(strlen(command) + 1);
            strcpy(result, command);
            return result;
        }
    }
    else if (isPath == 0)
    { // not a path. add the path manually

        char *userBinDir = "/usr/bin/";
        char *userBinPath = malloc(strlen(command) + strlen(userBinDir) + 1);
        strcpy(userBinPath, userBinDir);
        strcat(userBinPath, command);

        if (access(userBinPath, X_OK) != -1)
        {                       // found
            return userBinPath; // make sure to free from caller
        }
        free(userBinPath);

        char *binDir = "/bin/";
        char *binPath = malloc(strlen(command) + strlen(binDir) + 1);
        strcpy(binPath, binDir);
        strcat(binPath, command);
        if (access(binPath, X_OK) != -1)
        {                   // found
            return binPath; // make sure to free from caller
        }
        free(binPath);

        printf("%s: command not found\n", command);
    }
    return NULL;
}

int handleBuiltInFunction(char **shellArgs, int argSize)
{
    char *command = shellArgs[0];
    if (strcmp(command, "jobs") == 0)
    {
        printJobs();
        return 1;
    }
    else if (strcmp(command, "bg") == 0)
    {
        // run suspended job in the background, shellArgs[1] should contain job id
        if (shellArgs[1] == NULL || shellArgs[1][0] != '%' || !isdigit(shellArgs[1][1]))
        {
            printf("invalid bg syntax\n");
            return 1;
        }
        memmove(shellArgs[1], shellArgs[1] + 1, strlen(shellArgs[1]));
        Job *temp = jobList;
        while (temp != NULL)
        {
            if (temp->jobID == atoi(shellArgs[1]))
            {
                kill(temp->processID, SIGCONT);
                temp->isBackground = 1;
                free(temp->status);
                temp->status = malloc(strlen("Running") + 1);
                strcpy(temp->status, "Running");
                return 1;
            }
            temp = temp->next;
        }

        return 1;
    }
    else if (strcmp(command, "fg") == 0)
    {
        // run suspended job in the background, shellArgs[1] should contain job id
        if (shellArgs[1] == NULL || shellArgs[1][0] != '%' || !isdigit(shellArgs[1][1]))
        {
            printf("invalid bg syntax\n");
            return 1;
        }
        memmove(shellArgs[1], shellArgs[1] + 1, strlen(shellArgs[1]));
        Job *temp = jobList;
        while (temp != NULL)
        {
            if (temp->jobID == atoi(shellArgs[1]))
            {
                int status;
                kill(temp->processID, SIGCONT);
                currentPID = temp->processID;
                waitpid(temp->processID, &status, WUNTRACED);
                if (WIFSTOPPED(status))
                { // terminated by ctr+z. job is suspended
                    free(temp->status);
                    temp->status = malloc(strlen("Stopped") + 1);
                    strcpy(temp->status, "Stopped");
                }
                else
                {
                    removeJob(temp->processID);
                }
                return 1;
            }
            temp = temp->next;
        }

        return 1;
    }

    else if (strcmp(command, "cd") == 0)
    {
        if (argSize > 0)
        {
            if (chdir(shellArgs[1]) == -1)
            {
                if (errno == ENOTDIR)
                {
                    printf("Not a directory\n");
                }
                else
                {
                    printf("No such file or directory\n");
                }
            }
        }
        else
        {
            chdir(getenv("HOME"));
        }

        return 1;
    }
    else if (strcmp(command, "kill") == 0)
    {
        if (shellArgs[1] == NULL || shellArgs[1][0] != '%' || !isdigit(shellArgs[1][1]))
        {
            printf("invalid bg syntax\n");
            return 1;
        }
        memmove(shellArgs[1], shellArgs[1] + 1, strlen(shellArgs[1]));
        Job *temp = jobList;
        while (temp != NULL)
        {
            if (temp->jobID == atoi(shellArgs[1]))
            {
                kill(temp->processID, SIGTERM);
                printf("[%d] %d terminated by signal %d\n", temp->jobID, temp->processID, SIGTERM);
                removeJob(temp->processID);
                return 1;
            }
            temp = temp->next;
        }

        return 1;
    }

    return 0;
}

/**
 * MAIN FUNCTION
 * ------------------------------------------------
 */
int main(int argc, char *argv[])
{
    signal(SIGINT, sigint_handler);
    signal(SIGTSTP, sigstp_handler);
    signal(SIGCHLD, sigchild_handler);

    int defaultBufSize = 1024;
    char *line = NULL;
    size_t bufsize;

    while (1)
    {
        printf("> ");
        int x = getline(&line, &bufsize, stdin);
        if (x == -1)
        {
            printf("\n");
            char *mockCommand = malloc(1);
            char **shellArgs = calloc(defaultBufSize, sizeof(char *));
            exitWithoutLeaks(shellArgs, 1024, line, mockCommand);
        }
        // BUG: NEED TO HANDLE EMPTY INPUT
        char *delimiter = " \n\t\r";
        char *splitInput = strtok(line, delimiter);
        char **shellArgs = calloc(defaultBufSize, sizeof(char *));
        int shellArgSize = defaultBufSize;
        int argIndex = 0;
        int bgFlag = 0;
        int inputFlag = 1;

        while (splitInput != NULL)
        {
            inputFlag = 1;
            for (int i = 0; i < strlen(splitInput); i++)
            {
                if (!isspace(splitInput[i]))
                {
                    inputFlag = 0;
                    break;
                }
            }
            if (inputFlag)
            {
                for (int i = 0; i < shellArgSize; i++)
                {
                    free(shellArgs[i]);
                }
                break;
            }

            if (argIndex >= defaultBufSize)
            {
                shellArgSize += defaultBufSize;
                shellArgs = realloc(shellArgs, shellArgSize);
                if (!shellArgs)
                {
                    printf("Realloc Error\n");
                    exit(EXIT_FAILURE);
                }
            }
            // if (splitInput != "\n")
            // printf("Comare: %s, %s, %d\n", splitInput, "\n", isspace("\n"));
            // if (strcmp(splitInput, "\n"))
            char *duplicated = malloc(sizeof(char) * (strlen(splitInput) + 1));
            strcpy(duplicated, splitInput);
            shellArgs[argIndex] = duplicated;
            // shellArgs[argIndex] = strdup(splitInput);
            argIndex++;
            // printf("%s\n", splitInput);
            // splitInput = strtok(NULL, " ");
            splitInput = strtok(NULL, delimiter);
        }
        free(splitInput);
        argIndex--;
        if (argIndex < 0)
        {
            for (int i = 0; i < shellArgSize; i++)
            {
                free(shellArgs[i]);
            }
            free(shellArgs);
            continue;
        }
        int lastStringLength = strlen(shellArgs[argIndex]);
        // int spacing = argIndex > 0 ? 2 : 1;

        int spacing = 1;
        if (shellArgs[argIndex][0] == '&')
        {
            free(shellArgs[argIndex]);
            argIndex--;
            shellArgSize = argIndex;
            bgFlag = 1;
            // printf("1: Recognized background process %d \n", bgFlag);
        }
        else if (shellArgs[argIndex][lastStringLength - spacing] == '&')
        {
            shellArgs[argIndex][lastStringLength - spacing] = '\0';
            bgFlag = 1;
            // printf("2: Recognized background process %d \n", bgFlag);
        }

        if (strcmp(shellArgs[0], "exit") == 0)
        {
            char *mockCommand = malloc(1);
            exitWithoutLeaks(shellArgs, shellArgSize, line, mockCommand);
        }

        int isBuiltInCommand = handleBuiltInFunction(shellArgs, argIndex);
        if (isBuiltInCommand)
        {
            char *mockCommand = malloc(1);
            garbageTruck(shellArgs, shellArgSize, line, mockCommand);
            continue;
        }

        char *command = parsePath(shellArgs); // Should accept all args and then parse
        if (command == NULL)
        {
            garbageTruck(shellArgs, shellArgSize, line, command);
            continue;
        }

        execute(shellArgs, command, bgFlag, argIndex);

        // Check to see if shell program recieves a SIGINT or SIGTSTP signal and send those signals to processes running in the foreground

        // Some possibly useful functions: access, getenv, setenv, wait, waitpid, kill, signal, fork, execve, setpgid, sigprocmask, sleep.
        // printf("Input: %s\n", line, x);
        garbageTruck(shellArgs, shellArgSize, line, command);
    }
}