# Custom Shell

This document provide instructions for compiling and running the Custom Shell, a command-line interface for interacting with the operating system.

## Getting Started

Follow these instructions to set up and run the Custom Shell on a macOS system.

### Prerequisites

Ensure you have the following prerequisites installed:
- A C++ compiler (Clang is recommended for macOS)
- Make (for executing makefile commands)

### Program Execution Instructions

#### 1. Compiling the Program
   - Navigate to the root directory of the program.
   - Run the command: 
     ```
     make shell
     ```
     This will compile the shell program.

#### 2. Running the Program
   - After compilation, execute the shell by running:
     ```
     ./shell
     ```
     This will start the shell in interactive mode.

#### 3. Additional Notes
   - In case of issues, such as corrupted binary executables or modified initial configuration, clean up the build files by running:
     ```
     make clean
     ```
     This will remove all built files, allowing a fresh compilation.


### Supported Features

Currently, this custom shell supports the following features, similar to a regular shell:
- command lists, the `cd` command
- conditionals
- pipelines
- background processes
- zombie processes
- redirections
- interruption
- subshells
- variable substitution 
