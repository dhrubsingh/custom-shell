#include "shell.hh"
#include <cstring>
#include <cerrno>
#include <vector>
#include <sys/stat.h>
#include <sys/wait.h>
#include <map>

#include <iostream>

// For the love of God
#undef exit
#define exit __DO_NOT_CALL_EXIT__READ_PROBLEM_SET_DESCRIPTION__


volatile sig_atomic_t sig_rec = 0;

void sig_handle(int signal) {
    sig_rec = 1;
}

std::map<std::string, std::string> env_var; // store environment variables for variable substitution

// function to implement variable substitution
void sub_vars(std::string& arg, const std::map<std::string, std::string>& variables) {
    size_t position = 0;

    // go through the input string until you find the $ for variable start
    while ((position = arg.find('$', position)) != std::string::npos) {
        // find the end of the variable through a space
        size_t end = arg.find(" ", position + 1);

        // get the variable name 
        std::string name = arg.substr(position + 1, end - position - 1);

        // try to find the variable names in the map
        auto it = variables.find(name);
        if (it != variables.end()) {
            arg.replace(position, end - position, it->second); // substittue the variable value from the map
        }
        position += it->second.size(); // search for more variables
    }
}

struct command;
struct conditional;
struct command {
    std::vector<std::string> args;
    pid_t pid = -1; // process ID running this command, -1 if none

    command* next_in_pipeline = nullptr;
    command* prev_in_pipeline = nullptr;
    
    int end_pipe = -1; // end pipe descriptor
    int status = -1; // exit status of command

    // Redirection metadata
    std::string file_in, file_out, file_err, append_stdout, append_stderr;
    int stat_in = -1, stat_out = -1, stat_err = -1;
    conditional* subshell_commands = nullptr;  // to hold all the subshell commands

    command(); // Constructor
    ~command(); // Destructor

    void run(); // Function to execute the command
    void redirect_errout(const std::string& file_type, int& file_descriptor, int target_descriptor); // redirect output/error function
    void redirect_inp(const std::string& file_type, int& file_descriptor, int target_descriptor); // redirect input function

};

struct pipeline {
      command* command_child = nullptr;
    pipeline* next_in_conditional = nullptr;
    bool next_is_or = false;

    int exit_status = 1;

    pipeline(); // Constructor
    ~pipeline(); // Destructor

};

struct conditional {
     pipeline* pipeline_child = nullptr;
    conditional* next_in_list = nullptr;
    bool is_background = false;

    conditional(); // Constructor
    ~conditional(); // Destructor

};

command::command(){

}

// command::~command()
//    This destructor function is called to delete a command.

command::~command() {
    // Delete the next command in the pipeline
    if (next_in_pipeline != nullptr) {
        delete next_in_pipeline;
    }
}


pipeline::pipeline() {
    command_child = new command;
}

pipeline::~pipeline() {
    // Delete the command child
    delete command_child;

    // Delete the next pipeline in the conditional chain
    if (next_in_conditional != nullptr) {
        delete next_in_conditional;
    }
}

conditional::conditional(){
    pipeline_child = new pipeline;
}

conditional::~conditional() {
    // Delete the pipeline child
    delete pipeline_child;

    // Delete the next conditional in the list
    if (next_in_list != nullptr) {
        delete next_in_list;
    }
}

void run_list(conditional* c);

// COMMAND EXECUTION

// command::run()
//    Creates a single child process running the command in `this`, and
//    sets `this->pid` to the pid of the child process.
//
//    If a child process cannot be created, this function should call
//    `_exit(EXIT_FAILURE)` (that is, `_exit(1)`) to exit the containing
//    shell or subshell. If this function returns to its caller,
//    `this->pid > 0` must always hold.
//

// redirect function for std_out and std_err
void command::redirect_errout(const std::string& file_type, int& file_descriptor, int target_descriptor) {
    if (!file_type.empty()) { // check for if there is a file for redirection
        file_descriptor = open(file_type.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666); // open the file with the appropriate permissions
        if (file_descriptor < 0) { // can't be opened, print error accordingly
            perror("open");
            _exit(EXIT_FAILURE);
        }
        dup2(file_descriptor, target_descriptor); // redirect from file descriptor to target descriptor
        close(file_descriptor); // close the file descriptor 
    }
}

// redirect function for std_in
void command::redirect_inp(const std::string& file_type, int& file_descriptor, int target_descriptor) {
    bool redirect_input = false; // tag to check whether the input has been redirected
    if (!file_type.empty()) { // check for if there is a file for redirection
        file_descriptor = open(file_type.c_str(), O_RDONLY); // open file as read only
        if (file_descriptor < 0) { // can't open print error
            perror("open");
            _exit(EXIT_FAILURE);
        }
        dup2(file_descriptor, target_descriptor); // redirect input from file
        close(this->stat_in); // close previously opened file descriptor
        redirect_input = true; // input redirection successful
    }
    if (!redirect_input && this->prev_in_pipeline) { // if not redirected from file and part of pipeline
            dup2(this->end_pipe, STDIN_FILENO); // handle redirection from pipeline
            close(this->end_pipe);
        }
}


void command::run() {
    // first substitute the arguments
    for (auto& arg : this->args) {
        sub_vars(arg, env_var);
    }

    // check if it's variable assignment
    if (this->args.size() == 1 && this->args[0].find('=') != std::string::npos) {
        env_var[this->args[0].substr(0, this->args[0].find('='))] = this->args[0].substr(this->args[0].find('=') + 1);  // store the variable into the map
        this->pid = -1;
        return;
    }

    // handle the subshell execution
    if (this->subshell_commands != nullptr) {
        pid_t sub_pid = fork();
        if (sub_pid < 0) {
            perror("fork");
            _exit(EXIT_FAILURE);
        }

        if (sub_pid == 0) { // handle the child process
            run_list(this->subshell_commands); // execute all commands
            _exit(EXIT_SUCCESS);
        } else { // parent process
            int status;
            if (waitpid(sub_pid, &status, 0) == -1) { // wait for subshell to complete
                perror("waitpid");
                _exit(EXIT_FAILURE);
            }
            // update the exit status
            if (WIFEXITED(status)){
                this->status = WEXITSTATUS(status);
            }
            else {
                this->status = -1;
            }
            return;
        }
    }

     // check for cd command to change state of shell process
    if (this->args[0] == "cd") {
      
        int error = dup(STDERR_FILENO); // store the original stderr
        
        // dup2 in case we have redirections 
        if (this->stat_err != -1) {
            dup2(this->stat_err, STDERR_FILENO);
            close(this->stat_err); 
        }

        // restore the stderr
        dup2(error, STDERR_FILENO);
        close(error);

        // update the status accordingly based on whether chdir executes correctly
        if (chdir(this->args[1].c_str()) == 0)
        {
            this->status = 0; // success, and chdir changes the directory
        }
        else
        {
            this->status = 1; // failure, and chdir doesn't change the directory
        }
        this->pid = getpid(); // set pid to the ID of the shell process since it's a built in command

        return;
    }

    assert(this->pid == -1);
    assert(this->args.size() > 0);

    int pipe_descriptors[2]; // create the pipeline descriptors
    int pipe_return = pipe(pipe_descriptors); // create the pipe
    if (this->next_in_pipeline) {
        if (pipe_return < 0) { // check if we sucessfully created the pipe
            perror("pipe"); 
            _exit(EXIT_FAILURE);
        }
    }
    
    pid_t current_process = fork();

    if (current_process < 0) {
        perror("fork");
        _exit(EXIT_FAILURE);
    }

    if (current_process == 0) { // child process

        // set process group for interrupts
        if (setpgid(0, 0) < 0) {
            perror("setpgid");
            _exit(EXIT_FAILURE);
        }

        // handle all the redirections
        redirect_inp(this->file_in, this->stat_in, STDIN_FILENO);
        redirect_errout(this->file_out, this->stat_out, STDOUT_FILENO);
        redirect_errout(this->file_err, this->stat_err, STDERR_FILENO);

        // redirect appends
        redirect_errout(this->append_stdout, this->stat_out, STDOUT_FILENO);
        redirect_errout(this->append_stderr, this->stat_err, STDERR_FILENO);

        if (this->file_out.empty() && this->next_in_pipeline ) {  // check if part of pipeline and dont override output redirection
            dup2(pipe_descriptors[1], STDOUT_FILENO); // duplicate write end of pipe to stdout
            close(pipe_descriptors[1]); // close end of pipe
            close(pipe_descriptors[0]); // close end of pipe
        }

        // convert arguments for excepvp
        size_t length_vector = this->args.size();
        char* input_commands[length_vector + 1];
        for (size_t i = 0; i < length_vector; ++i) {
            input_commands[i] = (char *)this->args[i].c_str();
        }
        input_commands[length_vector] = nullptr; // set last element as the nullptr

        // execute command
        if (execvp(input_commands[0], input_commands) == -1) { // check if execvp fails
            perror("execvp");
            _exit(EXIT_FAILURE);
        }
    
    } else { // in the parent process
            this->pid = current_process;
            
            // close write end of pipe
            if (this->next_in_pipeline) {
                close(pipe_descriptors[1]);
                this->next_in_pipeline->end_pipe = pipe_descriptors[0];  
            }
            // close read end of pipe
            if (this->prev_in_pipeline) {
                close(this->prev_in_pipeline->end_pipe);
            }
    }

}

// run_list(c)
//    Run the command *list* starting at `c`. Initially this just calls
//    `c->run()` and `waitpid`; you’ll extend it to handle command lists,
//    conditionals, and pipelines.
//
//    It is possible, and not too ugly, to handle lists, conditionals,
//    *and* pipelines entirely within `run_list`, but many students choose
//    to introduce `run_conditional` and `run_pipeline` functions that
//    are called by `run_list`. It’s up to you.

void run_pipeline(pipeline* p);
void run_conditional(conditional* c);

// function to go through all the conditionals
void run_list(conditional* c) { 
    while (c != nullptr) {

    // check if it's a background process
    if (c->is_background) {
        pid_t curr_pid = fork(); // fork and store it in pid variable
        if (curr_pid < 0) { 
            perror("fork");
            _exit(EXIT_FAILURE); // failure of fork
        } else if (curr_pid == 0) {  // in the child process
            
            // for interrupts, need to set child process to a new process group 
            if (setpgid(0, 0) < 0) { 
                _exit(EXIT_FAILURE);
            }

            run_conditional(c); // run the conditionals on that current conditional
            _exit(EXIT_SUCCESS);
        }
         usleep(12000); // delay 12 ms for background to catch up
    } else {
        run_conditional(c); // if it's not a background process, just run as usual
    }
        c = c->next_in_list; // traverse to the next conditionals
    }
}

void run_conditional(conditional* c) { // run pipelines given a conditional
    
    pipeline* curr_pipe = c->pipeline_child; // first pipeline of input conditional
    bool execute = true; // tag to check if we should execute next pipeline or not
    int status = 0; // exit status of previous command

    // go through the pipelines
    while (curr_pipe != nullptr) {
        if (execute) { // if we should execute
            run_pipeline(curr_pipe); // run the next pipeline

            // retreive last command exit status
            command* last_command = curr_pipe->command_child;
            while (last_command->next_in_pipeline != nullptr) {
                last_command = last_command->next_in_pipeline;
            }
            status = last_command->status; // update status
           
        } 

        // check if next pipeline in conditional
        if (!curr_pipe->next_in_conditional)
        {
            execute = false;
        }
        else
        {
            if (!curr_pipe->next_is_or)  // and condition -> set to true
            {
                execute = (status == 0);
            }

            else // or condition -> set to false
            {
                execute = (status != 0); 
            }
        }

        curr_pipe = curr_pipe->next_in_conditional; // traverse to the next conditional
    }
}



void run_pipeline(pipeline* p) {
    command* curr = p->command_child; // get the first command in the pipeline
    int prev_end = -1;
    pid_t child_pid = -1;  // id of the first child in the pipeline

    // run the commands
    while (curr != nullptr) {
        curr->run();

        if (child_pid == -1 && curr->pid > 0) { // get the pid of the first child
            child_pid = curr->pid;
        }

        if (prev_end != -1) { // check if there's a previous pipe and close accordingly
            close(prev_end);
        }

        prev_end = curr->end_pipe; // correctly update the pipe end from this command

        curr = curr->next_in_pipeline; // move to the next pipeline
    }

    // wait for only last command to not finish
    command* last_command = p->command_child;
    while (last_command->next_in_pipeline != nullptr) {
        last_command = last_command->next_in_pipeline;
    }

    bool is_cd = last_command->pid > 0 && last_command->pid != getpid();  // only wait if a child process was actually created and not the shell itself

    if (is_cd) { 
        if (child_pid > 0) { // set the foreground process
            claim_foreground(child_pid);
        }

        int status;
        if (waitpid(last_command->pid, &status, 0) == -1) {
            perror("waitpid");
            _exit(EXIT_FAILURE);
        }

        claim_foreground(0); // detach after waiting

        if (WIFEXITED(status)) {
            last_command->status = WEXITSTATUS(status); // update the exit status
        }
    }
}

// parse_line(s)
//    Parse the command list in `s` and return it. Returns `nullptr` if
//    `s` is empty (only spaces). You’ll extend it to handle more token
//    types.


// extracting parsing logic in parse_line to recursively handle parsing of subshells
conditional* parse_all_commands(shell_token_iterator& it, const shell_token_iterator& end) {
    // create initial links and hierearchy for the tree (conditonals -> pipelines -> commands)
    conditional* first_conditional = new conditional(); // first conditional is the root conditional in the tree
    conditional* current_conditonal = first_conditional;
    pipeline* current_pipeline = current_conditonal->pipeline_child;
    command* current_command = current_pipeline->command_child;

    for (; it != end; ++it) {
        switch (it.type()) {
            case TYPE_REDIRECT_OP: {
                auto next_it = it;
                ++next_it;
                // only proceed if not at the end and normal
                if (next_it != end && next_it.type() == TYPE_NORMAL) {
                    std::string file_parse = next_it.str();
                    // parsing the redirections
                    if (it.str() == "<") { // input redirection
                        current_command->file_in = file_parse; // set file_in as file_parse
                    } else if (it.str() == ">") {
                        current_command->file_out = file_parse; // set file_out as file_parse
                    } else if (it.str() == "2>") {
                        current_command->file_err = file_parse; // set file_err as file_parse
                    } else if (it.str() == "2>>") { // set append_stderr as file_parse
                        current_command->append_stderr = file_parse;
                    } else if (it.str() == ">>") { // set append_stdout as file_parse for another redirection
                        current_command->append_stdout = file_parse;
                    }
                    
                    ++it; // increment to next token
                }
                break;
            }

            case TYPE_NORMAL:
                current_command->args.push_back(it.str()); // add arugments to command
                break;

            case TYPE_PIPE:
                
                current_command->next_in_pipeline = new command(); // create a new command after ending the current command
                current_command->next_in_pipeline->prev_in_pipeline = current_command; // link back to current command
                current_command = current_command->next_in_pipeline; // point current commmand to new command
                break;

            case TYPE_SEQUENCE:
            case TYPE_BACKGROUND:{ // set the according background tag and end current command
                current_command = nullptr; // end current command
                if (it.type() == TYPE_BACKGROUND) {
                    current_conditonal->is_background = true; // update for whether it's a background process 
                }

                // if not, check if more tokens to parse
                auto next_it = it;
                ++next_it; 
                // create new conditional and update the pointers for these objects accordingly
                if (next_it != end) { 
                    current_conditonal->next_in_list = new conditional(); 
                    current_conditonal = current_conditonal->next_in_list;
                    current_pipeline = current_conditonal->pipeline_child;
                    current_command = current_pipeline->command_child;
                }
                break;
            }

            case TYPE_AND:
            case TYPE_OR:
                // end the current command and set whether it is AND or OR by looking if type == TYPE_OR
                current_command = nullptr; // end current command
                current_pipeline->next_is_or = (it.type() == TYPE_OR); // check whether it's and or not

                // create new commands and pipelines and link them
                if (!current_pipeline->next_in_conditional) {
                    current_pipeline->next_in_conditional = new pipeline();   
                }
                current_pipeline = current_pipeline->next_in_conditional; // link to point to current pipeline to new pipeline
                current_command = current_pipeline->command_child; // link to point to new command
                break;

            case TYPE_LPAREN: {
                auto sub_it = it;
                ++sub_it;
                conditional* subshell_conditional = parse_all_commands(sub_it, end);
                current_command->subshell_commands = subshell_conditional;
                it = sub_it;
                break;
            }

        }
    }

    return first_conditional; // return the root of the tree
}


conditional* parse_line(const char* s) {
    shell_parser parser(s);
    auto iterator = parser.begin();
    return parse_all_commands(iterator, parser.end());
}



int main(int argc, char* argv[]) {
    FILE* command_file = stdin;
    bool quiet = false;

    // Check for `-q` option: be quiet (print no prompts)
    if (argc > 1 && strcmp(argv[1], "-q") == 0) {
        quiet = true;
        --argc, ++argv;
    }

    // Check for filename option: read commands from file
    if (argc > 1) {
        command_file = fopen(argv[1], "rb");
        if (!command_file) {
            perror(argv[1]);
            return 1;
        }
    }

    // Set the shell into the foreground and ignore SIGTTOU
    claim_foreground(0);
    set_signal_handler(SIGTTOU, SIG_IGN);

    // Set up SIGINT signal handler
    signal(SIGINT, sig_handle);

    char buf[BUFSIZ];
    int bufpos = 0;
    bool needprompt = true;

    while (!feof(command_file)) {
        // Print the prompt at the beginning of the line
        if (needprompt && !quiet) {
            printf("shell[%d]$ ", getpid());
            fflush(stdout);
            needprompt = false;
        }

        // Read a string, checking for error or EOF
        if (fgets(&buf[bufpos], BUFSIZ - bufpos, command_file) == nullptr) {
            if (ferror(command_file) && errno == EINTR) {
                clearerr(command_file);
                buf[bufpos] = 0;
            } else {
                if (ferror(command_file)) {
                    perror("shell");
                }
                break;
            }
        }

        // If a complete command line has been provided, run it
        bufpos = strlen(buf);
        if (bufpos == BUFSIZ - 1 || (bufpos > 0 && buf[bufpos - 1] == '\n')) {
            if (conditional* c = parse_line(buf)) {
                run_list(c);
                delete c;
            }
            
            bufpos = 0;
            needprompt = 1;
        }

        // check for SIGINT for interrupts
        if (sig_rec) {
            sig_rec = 0;
            fflush(stdout);
            needprompt = true;
            bufpos = 0;
        }
        // handle zombie processes
        while (waitpid(-1, NULL, WNOHANG) > 0) {
        }
    }

    return 0;
}