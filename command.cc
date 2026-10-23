#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include <glob.h>
#include <wordexp.h>
#include <vector>
#include <string>
#include <cctype>
#include <iostream>
#include <sstream>
#include "command.h"
#include "tokenizer.h"


static int logFile;

static void writeLog(pid_t pid)
{
    dprintf(logFile, "Child process %d terminated\n", pid);
}

static void childDone(int)
{
    pid_t pid;
    while ((pid = waitpid(-1, NULL, WNOHANG)) > 0)
        writeLog(pid);
}

static void redirectTo(const char *file, int flags, int target)
{
    int fd = open(file, flags, 0644);
    if (fd < 0)
    {
        perror(file);
        _exit(1);
    }
    dup2(fd, target);
    close(fd);
}

static void abortParse(SimpleCommand *simple, const char *message)
{
    Command::_currentCommand.insertSimpleCommand(simple);
    Command::_currentCommand.clear();
    fprintf(stderr, "myshell: %s\n", message);
}

void parse(std::vector<Token> &tokens)
{
    if (tokens.size() == 1)
        return;

    Command &currentCommand = Command::_currentCommand;
    SimpleCommand *currentSimpleCommand = new SimpleCommand();

    for (size_t i = 0; tokens[i].type != TOKEN_EOF; i++)
    {
        TokenType type = tokens[i].type;

        if (type == TOKEN_COMMAND || type == TOKEN_ARGUMENT)
        {
            std::string word = tokens[i].value;
            bool quoted = false;

            if (word[0] == '"' || word[0] == '\'')
            {
                char quote = word[0];
                quoted = true;
                word.erase(0, 1);
                while (word.empty() || word.back() != quote)
                {
                    if (tokens[i + 1].type == TOKEN_EOF)
                    {
                        abortParse(currentSimpleCommand, "unterminated quote");
                        return;
                    }
                    i++;
                    word += " " + tokens[i].value;
                }
                word.pop_back();
            }

            if (!quoted && word.find_first_of("*?") != std::string::npos)
            {
                glob_t matches;
                glob(word.c_str(), GLOB_NOCHECK, NULL, &matches);
                for (size_t k = 0; k < matches.gl_pathc; k++)
                    currentSimpleCommand->insertArgument(strdup(matches.gl_pathv[k]));
                globfree(&matches);
            }
            else
                currentSimpleCommand->insertArgument(strdup(word.c_str()));
        }
        else if (type == TOKEN_PIPE)
        {
            if (currentSimpleCommand->_numberOfArguments == 0)
            {
                abortParse(currentSimpleCommand, "missing command before '|'");
                return;
            }
            currentCommand.insertSimpleCommand(currentSimpleCommand);
            currentSimpleCommand = new SimpleCommand();
        }
        else if (type == TOKEN_BACKGROUND)
        {
            if (tokens[i + 1].type != TOKEN_EOF)
            {
                abortParse(currentSimpleCommand, "'&' must be the last token of the command");
                return;
            }
            currentCommand._background = 1;
        }
        else
        {
            if (tokens[i + 1].type != TOKEN_COMMAND && tokens[i + 1].type != TOKEN_ARGUMENT)
            {
                std::string message = "missing file name after '" + tokens[i].value + "'";
                abortParse(currentSimpleCommand, message.c_str());
                return;
            }

            char *file = strdup(tokens[i + 1].value.c_str());
            if (type == TOKEN_INPUT)
            {
                free(currentCommand._inputFile);
                currentCommand._inputFile = file;
            }
            else if (type == TOKEN_ERROR)
            {
                free(currentCommand._errFile);
                currentCommand._errFile = file;
                currentCommand._out_error = 0;
            }
            else
            {
                free(currentCommand._outFile);
                currentCommand._outFile = file;
                currentCommand._append = (type != TOKEN_REDIRECT);
                if (type == TOKEN_REDIRECT_AND_ERROR)
                {
                    free(currentCommand._errFile);
                    currentCommand._errFile = strdup(file);
                    currentCommand._out_error = 1;
                }
            }
            i++;
        }
    }

    if (currentSimpleCommand->_numberOfArguments == 0)
    {
        abortParse(currentSimpleCommand, currentCommand._numberOfSimpleCommands > 0
                                             ? "missing command after '|'"
                                             : "missing command");
        return;
    }

    currentCommand.insertSimpleCommand(currentSimpleCommand);
    currentCommand.execute();
}

SimpleCommand::SimpleCommand()
{
    _numberOfAvailableArguments = 5;
    _numberOfArguments = 0;
    _arguments = (char **)malloc(_numberOfAvailableArguments * sizeof(char *));
}

void SimpleCommand::insertArgument(char *argument)
{
    if (_numberOfAvailableArguments == _numberOfArguments + 1)
    {
        _numberOfAvailableArguments *= 2;
        _arguments = (char **)realloc(_arguments,
                                      _numberOfAvailableArguments * sizeof(char *));
    }

    _arguments[_numberOfArguments] = argument;
    _arguments[_numberOfArguments + 1] = NULL;

    _numberOfArguments++;
}

Command::Command()
{
    _numberOfAvailableSimpleCommands = 1;
    _simpleCommands = (SimpleCommand **)
        malloc(_numberOfAvailableSimpleCommands * sizeof(SimpleCommand *));

    _numberOfSimpleCommands = 0;
    _outFile = 0;
    _inputFile = 0;
    _errFile = 0;
    _background = 0;
    _out_error = 0;
    _append = 0;
}

void Command::insertSimpleCommand(SimpleCommand *simpleCommand)
{
    if (_numberOfAvailableSimpleCommands == _numberOfSimpleCommands)
    {
        _numberOfAvailableSimpleCommands *= 2;
        _simpleCommands = (SimpleCommand **)realloc(_simpleCommands,
                                                    _numberOfAvailableSimpleCommands * sizeof(SimpleCommand *));
    }

    _simpleCommands[_numberOfSimpleCommands] = simpleCommand;
    _numberOfSimpleCommands++;
}

void Command::clear()
{
    for (int i = 0; i < _numberOfSimpleCommands; i++)
    {
        for (int j = 0; j < _simpleCommands[i]->_numberOfArguments; j++)
        {
            free(_simpleCommands[i]->_arguments[j]);
        }

        free(_simpleCommands[i]->_arguments);
        delete _simpleCommands[i];
    }

    if (_outFile)
    {
        free(_outFile);
    }

    if (_inputFile)
    {
        free(_inputFile);
    }

    if (_errFile)
    {
        free(_errFile);
    }

    _numberOfSimpleCommands = 0;
    _outFile = 0;
    _inputFile = 0;
    _errFile = 0;
    _background = 0;
    _out_error = 0;
    _append = 0;
}

void Command::print()
{
    printf("\n\n");
    printf("              COMMAND TABLE                \n");
    printf("\n");
    printf("  #   Simple Commands\n");
    printf("  --- ----------------------------------------------------------\n");

    for (int i = 0; i < _numberOfSimpleCommands; i++)
    {
        printf("  %-3d ", i);
        for (int j = 0; j < _simpleCommands[i]->_numberOfArguments; j++)
        {
            printf("\"%s\" \t", _simpleCommands[i]->_arguments[j]);
        }
    }

    printf("\n\n");
    printf("  Output       Input        Error        Err&Out       Background\n");
    printf("  ------------ ------------ ------------ ------------ ------------\n");
    printf("  %-12s %-12s %-12s %-12s %-12s\n", _outFile ? _outFile : "default",
           _inputFile ? _inputFile : "default", _errFile ? _errFile : "default", _out_error == 1 ? _errFile : "default"
           ,_background ? "YES" : "NO");
    printf("\n\n");
}

void Command::execute()
{
    print();

    char **args = _simpleCommands[0]->_arguments;
    if (_numberOfSimpleCommands == 1)
    {
        if (strcmp(args[0], "exit") == 0)
        {
            printf("Good bye!!\n");
            exit(0);
        }
        if (strcmp(args[0], "cd") == 0)
        {
            if (chdir(args[1] ? args[1] : getenv("HOME")) != 0)
                perror("cd");
            clear();
            return;
        }
    }

    int count = _numberOfSimpleCommands;
    int input = 0;
    pid_t lastPid = 0;

    for (int i = 0; i < count; i++)
    {
        int fds[2];
        if (i < count - 1)
            pipe(fds);

        args = _simpleCommands[i]->_arguments;
        lastPid = fork();
        if (lastPid == 0)
        {
            signal(SIGINT, SIG_DFL);

            if (input != 0)
            {
                dup2(input, 0);
                close(input);
            }
            if (i < count - 1)
            {
                dup2(fds[1], 1);
                close(fds[0]);
                close(fds[1]);
            }
            if (i == 0 && _inputFile)
                redirectTo(_inputFile, O_RDONLY, 0);
            if (i == count - 1)
            {
                if (_outFile)
                {
                    redirectTo(_outFile, O_WRONLY | O_CREAT | (_append ? O_APPEND : O_TRUNC), 1);
                    if (_out_error)
                        dup2(1, 2);
                }
                if (_errFile && !_out_error)
                    redirectTo(_errFile, O_WRONLY | O_CREAT | O_TRUNC, 2);
            }

            execvp(args[0], args);
            fprintf(stderr, "myshell: %s: command not found\n", args[0]);
            _exit(1);
        }

        if (input != 0)
            close(input);
        if (i < count - 1)
        {
            close(fds[1]);
            input = fds[0];
        }
    }

    if (_background)
        printf("[%d]\n", lastPid);
    else if (waitpid(lastPid, NULL, 0) > 0)
        writeLog(lastPid);

    clear();
}

void Command::prompt()
{
    std::string input;
    while (true)
    {
        printf("myshell>");
        fflush(stdout);
        if (!std::getline(std::cin, input))
            break;
        std::vector<Token> tokens = tokenize(input);
        parse(tokens);
    }
}

Command Command::_currentCommand;
SimpleCommand *Command::_currentSimpleCommand;

int main()
{
    logFile = open("log.txt", O_WRONLY | O_CREAT | O_APPEND, 0644);
    signal(SIGINT, SIG_IGN);
    signal(SIGCHLD, childDone);
    Command::_currentCommand.prompt();
    return 0;
}
