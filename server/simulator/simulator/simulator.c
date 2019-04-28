#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <sys/prctl.h>
#include <signal.h>
#include <unistd.h>
#include <cjson/cJSON.h>

#include "compile.h"
#include "vs.h"
#include "simulator.h"
#include "node.h"

// this function is a debugging function which creates a string of the arduino code
char* get_input() {
    char *string = (char*)malloc(1 * sizeof(char));
    int n_characters = 0;
    string[n_characters] = '\0';

    char c;

    while((c = getchar()) != EOF) {
        n_characters++;
        string = (char*)realloc(string, (n_characters + 1) * sizeof(char));
        string[n_characters] = '\0';
        string[n_characters - 1] = c;
    }

    return string;
}

char* get_id(cJSON *json) {
    while(json != NULL) {
        if(!strcmp(json->string, "id")) {
            return json->valuestring;
        }

        json = json->next;
    }

    error("Unable to get id.");
}

char* get_code(cJSON *json) {
    while(json != NULL) {
        if(!strcmp(json->string, "code")) {
            return json->valuestring;
        }

        json = json->next;
    }

    error("Unable to get code.");
}

cJSON* get_randomization(cJSON *json) {
    while(json != NULL) {
        if(!strcmp(json->string, "randomization")) {
            return json->child;
        }

        json = json->next;
    }

    error("Unable to get randomization.");
}

cJSON* clean_for_simulate(cJSON *json) {
    // get rid of type and code
    json = json->next->next;
    // keep randomization, distance sensors, id
    return json;
}

int ngets(char *new_buffer, int fd) {
    int size = read(fd, new_buffer, BUFFER_SIZE);
    if(size == -1) {
        return 0;
    } else {
        return size;
    }
}

struct process copen(char *command) {
    char *argv[] = { NULL };
    int in_pipe[2];
    int out_pipe[2];

    if(pipe(in_pipe) < 0) {
        error("Unable to pipe.");
    }

    if(pipe(out_pipe) < 0) {
        error("Unable to pipe.");
    }

    int pid = fork();
    switch(pid) {
        case -1:
        // this error
        error("Unable to fork.");
        case 0:
        // this is child
        // we want stdout -> pipe, stdin -> pipe
        close(out_pipe[1]);
        close(in_pipe[0]);

        dup2(out_pipe[0], fileno(stdin));
        dup2(in_pipe[1], fileno(stdout));

        // ask kernel to deliver SIGTERM in case the parent dies
        prctl(PR_SET_PDEATHSIG, SIGTERM);

        // run the command
        if(execvp(command, argv) == -1) {
            error("An error occured in execvp");
        }

        exit(0);
        break;
        default:
        // this is parent with child pid
        close(out_pipe[0]);
        close(in_pipe[1]);
        break;
    }

    struct process p;
    p.pid = pid;
    p.input_fd = in_pipe[0];
    p.output_fd = out_pipe[1];

    return p;
}

void cclose(struct process p) {
    kill(p.pid, SIGKILL);
}

unsigned long time_sec() {
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    return t.tv_sec;
}

unsigned long time_nsec() {
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    return t.tv_nsec;
}

int main(int argc, char *argv[]) {
    char *input = get_input();
    cJSON *json = cJSON_Parse(input);

    if(json == NULL) {
        fprintf(stderr, "Unable to parse JSON.\n");
        fflush(stderr);
        return -1;
    }

    cJSON *parent_json = json;
    cJSON *child_json = json->child;

    // now that we have the JSON we need to perform initialization
    if(initialize(get_id(child_json), get_code(child_json)) != 0) {
        // initialize error:
        return -1;
    }

    child_json = clean_for_simulate(child_json);
    parent_json->child = child_json;
    char *json_output = cJSON_Print(parent_json);
    init(json_output);
    free(json_output);

    // we have to run the processs
    char *command = (char*)malloc((strlen("./../environments//") + 2 * strlen(get_id(child_json))));
    sprintf(command, "./../environments/%s/%s", get_id(child_json), get_id(child_json));

    struct node *head = NULL;
    struct node *curr = head;

    unsigned long start = time_sec();
    unsigned long curr_sec = time_sec();
    unsigned long curr_nsec = time_nsec();

    struct process p = copen(command);
    free(command);
    fcntl(p.input_fd, F_SETFL, O_NONBLOCK);

    while(curr_sec - start < TIMEOUT_SEC) {
        while(time_nsec() - curr_nsec < FRAME_RATE_NSEC);
        // This itteration happens each frame

        char *curr_buff = (char*)malloc(BUFFER_SIZE * sizeof(char));
        int curr_size = ngets(curr_buff, p.input_fd);
        if(curr_size == 0) {
            free(curr_buff);
        } else {
            // we want to create a linked list of packets (queue)
            // resize for space (?)
            curr_buff = (char*)realloc(curr_buff, curr_size * sizeof(char));
            if(head == NULL) {
                head = new_node(curr_buff, curr_size);
                curr = head;
            } else {
                curr->next = new_node(curr_buff, curr_size);
                curr = curr->next;
            }
        }

        head = frame(head, p);
        curr_sec = time_sec();
        curr_nsec = time_nsec();
    }

    cclose(p);
    cJSON_Delete(parent_json);
    free(input);
    fflush(stdout);
    return 0;
}