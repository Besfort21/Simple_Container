#include <iostream>
#include <sched.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <vector>

#define STACK_SIZE (1024 * 1024)  // Stack size for cloned child

static char child_stack[STACK_SIZE];

struct child_args {
    char **argv;
};

int child_main(void *args) {
    struct child_args *child_args = (struct child_args *)args;

    // Set hostname
    sethostname("container", 10);

    // Remount proc to get accurate process info in the container
    mount("proc", "/proc", "proc", 0, NULL);

    // Execute the command
    execvp(child_args->argv[0], child_args->argv);

    // If execvp returns, it must have failed
    perror("execvp");
    return 1;
}

void write_to_cgroup(const std::string &path, const std::string &value) {
    std::ofstream ofs(path);
    if (ofs.is_open()) {
        ofs << value;
        ofs.close();
    } else {
        std::cerr << "Error opening " << path << std::endl;
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <command> [<args>...]" << std::endl;
        return 1;
    }

    struct child_args args;
    args.argv = &argv[1];

    // Create a new process in a new namespace
    pid_t pid = clone(child_main, child_stack + STACK_SIZE, 
                      SIGCHLD | CLONE_NEWUTS | CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWUSER, &args);
    if (pid == -1) {
        perror("clone");
        return 1;
    }

    // Set up cgroups
    std::string cgroup_path = "/sys/fs/cgroup/mycontainer/";
    std::system("mkdir -p /sys/fs/cgroup/mycontainer");
    
    write_to_cgroup(cgroup_path + "pids.max", "10"); // Limit to 10 processes
    write_to_cgroup(cgroup_path + "notify_on_release", "1");
    write_to_cgroup(cgroup_path + "cgroup.procs", std::to_string(pid));

    // Wait for the child process to terminate
    waitpid(pid, NULL, 0);

    // Clean up cgroup
    std::system("rmdir /sys/fs/cgroup/mycontainer");

    return 0;
}
