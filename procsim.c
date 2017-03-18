#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>
#include <string.h>



struct dispatch_queue {
    int size;
    struct dispatch_entry* head;
};

struct dispatch_entry {
    struct dispatch_entry* next;
    struct instruction* inst;
};

struct instruction {
    int op_type;
    int dest;
    int src1;
    int src2;
};

struct schedule_queue {
    int fu;
    int dest_reg;
    int dest_reg_tag;
    int src_reg1_ready;
    int src_reg1_tag;
    int src_reg1_value;
    int src_reg2_ready;
    int src_reg2_tag;
    int src_reg2_value;
    int busy;
    int inst_id;
};

struct register_file {
    int ready;
    int tag;
    int value;
};

struct scoreboard {
    struct func_unit* fu[3];
    int num_fus[3];
};

struct func_unit {
    int busy;
    int life;
    int inst_id;
    int reg;
    int tag;
    int value;
};

struct common_data_bus {
    int tag;
    int value;
    int reg;
    int busy;
};

void init_proc();
struct instruction* create_inst(int op_type, int dest_reg, int src_reg1, int src_reg2);
void add_to_disp_q(struct instruction* inst);
void fetch_instructions();
void dispatch(int cycle_half);
void run_proc();
void clear_not_ready_buf();
void schedule(int cycle_half);
void execute(int cycle_half);
void update_reg_file(int cycle_half);
void update_fu_status(int k, int type);

int R;
int F;
int K0;
int K1;
int K2;
int SCHED_Q_SIZE;
FILE* fp;
static const uint64_t DEFAULT_R = 2;
static const uint64_t DEFAULT_K0 = 3;
static const uint64_t DEFAULT_K1 = 2;
static const uint64_t DEFAULT_K2 = 1;
static const uint64_t DEFAULT_F = 4;
static const uint64_t REG_FILE_SIZE = 128;


struct dispatch_queue* disp_q;
struct schedule_queue* sched_q;
int* free_list;
int* not_ready_regs;
int* inst_retire_list;
struct register_file* registers;
int next_tag;
int next_inst_id;
struct scoreboard* scoreboard;
struct common_data_bus* CDB;



int main(int argc, char *argv[]) {
    R = DEFAULT_R;
    F = DEFAULT_F;
    K0 = DEFAULT_K0;
    K1 = DEFAULT_K1;
    K2 = DEFAULT_K2;

    //FILE* fp  = stdin;
    int opt;
    char *fileN;
    fp = fopen("hmmer.100k.trace", "r");

	while(-1 != (opt = getopt(argc, argv, "r:f:j:k:l:t"))) {
        
        switch(opt) {
        case 'r':
            R = atoi(optarg);
            break;
        case 'f':
            F = atoi(optarg);
            break;
        case 'j':
            K0 = atoi(optarg);
            break;
        case 'k':
            K1 = atoi(optarg);
            break;
        case 'l':
            K2 = atoi(optarg);
            break;
        case 't':
            fp = fopen("hmmer.100k.trace", "r");
            break;
        }
    } 

    
    init_proc();
    run_proc();
    fclose(fp);
}

void run_proc() {
    while (!feof(fp)) {
        fetch_instructions();
        for (int cycle_half = 1; cycle_half < 3; cycle_half++) {
            dispatch(cycle_half);
            schedule(cycle_half);
            execute(cycle_half);
            update_reg_file(cycle_half);
        }
    }
}

void init_proc() {
    SCHED_Q_SIZE = 2 * (K0 + K1 + K2); 
    next_tag = 0;
    next_inst_id = 0;
    if ((disp_q = calloc(1, sizeof(struct dispatch_queue))) == NULL) {
        perror("calloc");
        exit(EXIT_FAILURE);
    }
    if ((sched_q = calloc(SCHED_Q_SIZE, sizeof(struct schedule_queue))) == NULL) {
        perror("calloc");
        exit(EXIT_FAILURE);
    }

    if ((inst_retire_list = calloc(SCHED_Q_SIZE, sizeof(int))) == NULL) {
        perror("calloc");
        exit(EXIT_FAILURE);
    }

    if ((free_list = calloc(SCHED_Q_SIZE, sizeof(int))) == NULL) {
        perror("calloc");
        exit(EXIT_FAILURE);
    }

    if ((registers = calloc(REG_FILE_SIZE, sizeof(struct register_file))) == NULL) {
        perror("calloc");
        exit(EXIT_FAILURE);
    }

    if ((not_ready_regs = calloc(REG_FILE_SIZE, sizeof(int))) == NULL) {
        perror("calloc");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < REG_FILE_SIZE; i++) {
        registers[i].ready = 1;
    }

    if ((scoreboard = calloc(1, sizeof(struct scoreboard))) == NULL) {
        perror("calloc");
        exit(EXIT_FAILURE);
    }

    if ((scoreboard->fu[0] = calloc(K0, sizeof(struct func_unit))) == NULL) {
        perror("calloc");
        exit(EXIT_FAILURE);
    }

    if ((scoreboard->fu[1] = calloc(K1, sizeof(struct func_unit))) == NULL) {
        perror("calloc");
        exit(EXIT_FAILURE);
    }

    if ((scoreboard->fu[2] = calloc(K2, sizeof(struct func_unit))) == NULL) {
        perror("calloc");
        exit(EXIT_FAILURE);
    }

    scoreboard->num_fus[0] = K0;
    scoreboard->num_fus[1] = K1;
    scoreboard->num_fus[2] = K2;

    if ((CDB = calloc(R, sizeof(struct common_data_bus))) == NULL) {
        perror("calloc");
        exit(EXIT_FAILURE);
    }
}

struct instruction* create_inst(int op_type, int dest_reg, int src_reg1, int src_reg2) {
    struct instruction* inst = calloc(1, sizeof(struct instruction));
    inst->op_type = op_type;
    inst->dest = dest_reg;
    inst->src1 = src_reg1;
    inst->src2 = src_reg2;
    return inst;
}

void add_to_disp_q(struct instruction* inst) {
    struct dispatch_entry* entry;
    if ((entry = calloc(1, sizeof(struct dispatch_entry))) == NULL) {
        perror("calloc");
        exit(EXIT_FAILURE);
    }
    if (!disp_q->size) {
        disp_q->head = entry;
    } else {
        struct dispatch_entry* current_entry = disp_q->head;
        while (current_entry->next) {
            current_entry = current_entry->next;
        }
        current_entry->next = entry;
    }
    disp_q->size++;
    entry->inst = inst;
    entry->next = NULL;
}


void fetch_instructions() {
    int inst_count = 0;
    uint64_t address;
    int op_type;
    int dest_reg;
    int src_reg1;
    int src_reg2;
    while (inst_count < F && !feof(fp)) {
        int ret = fscanf(fp, "%" PRIx64 " %d %d %d %d \n", &address, &op_type, &dest_reg, &src_reg1, &src_reg2);
        if(ret == 5) {
            struct instruction* inst = create_inst(op_type, dest_reg, src_reg1, src_reg2);
            add_to_disp_q(inst);
        }
        inst_count++;
    }
}

void dispatch(int cycle_half) {
    clear_not_ready_buf();
    if (cycle_half == 1) {
        for(int i = 0; i < SCHED_Q_SIZE; i++) {
            if(!sched_q[i].busy && disp_q->size) {
                struct dispatch_entry* disp_entry = disp_q->head;
                disp_q->head = disp_q->head->next;
                disp_q->size--;

                sched_q[i].fu = disp_entry->inst->op_type;
                sched_q[i].dest_reg = disp_entry->inst->dest;
                //sched_q[i].inst_id = next_inst_id++;

                if (registers[disp_entry->inst->src1].ready) {
                    sched_q[i].src_reg1_value = registers[disp_entry->inst->src1].value;
                    sched_q[i].src_reg1_ready = 1;
                } else {
                    sched_q[i].src_reg1_tag = registers[disp_entry->inst->src1].tag;
                    sched_q[i].src_reg1_ready = 0;
                }

                if (registers[disp_entry->inst->src2].ready) {
                    sched_q[i].src_reg2_value = registers[disp_entry->inst->src2].value;
                    sched_q[i].src_reg2_ready = 1;
                } else {
                    sched_q[i].src_reg2_tag = registers[disp_entry->inst->src2].tag;
                    sched_q[i].src_reg2_ready = 0;
                }

                if (disp_entry->inst->dest > 0) {
                    registers[disp_entry->inst->dest].tag = next_tag; 
                    sched_q[i].dest_reg_tag = next_tag++;
                    not_ready_regs[disp_entry->inst->dest] = 1;
                }
                
            } 
            
            free_list[i] = sched_q[i].busy;
        }

    } else {
        for(int i = 0; i < REG_FILE_SIZE; i++) {
            if (not_ready_regs[i]) {
                registers[i].ready = 0;
            }
        }
    }
}

void clear_not_ready_buf() {
    for (int i = 0; i < REG_FILE_SIZE; i++) {
        not_ready_regs[i] = 0;
    }
}

void schedule(int cycle_half) {
    for (int i = 0; i < SCHED_Q_SIZE; i++) {
        if (cycle_half == 1) {
            //TODO: fired in order of increasing tag values
            if (sched_q[i].src_reg1_ready && sched_q[i].src_reg2_ready && sched_q[i].fu > 0) {
                for (int j = 0; j < scoreboard->num_fus[sched_q[i].fu]; j++) {
                    if (scoreboard->fu[sched_q[i].fu][j].busy == 0) {
                        scoreboard->fu[sched_q[i].fu][j].busy = 1; 
                        scoreboard->fu[sched_q[i].fu][j].life = 0;
                        scoreboard->fu[sched_q[i].fu][j].inst_id = i;
                        scoreboard->fu[sched_q[i].fu][j].reg = sched_q[i].dest_reg;
                        scoreboard->fu[sched_q[i].fu][j].tag = sched_q[i].dest_reg_tag;
                        break;
                    }
                }
            } else if (sched_q[i].src_reg1_ready && sched_q[i].src_reg2_ready && sched_q[i].fu < 0) {
                sched_q[i].busy = 0;
            }
        } else {
            for (int j = 0; j < R; j++) {
                if (CDB[j].tag == sched_q[i].src_reg1_tag) {
                    sched_q[i].src_reg1_ready = 1;
                    sched_q[i].src_reg1_value = CDB[j].value;
                } 
                if (CDB[j].tag == sched_q[i].src_reg2_tag) {
                    sched_q[i].src_reg2_ready = 1;
                    sched_q[i].src_reg2_value = CDB[j].value;
                }
            }
        }
    }   
}

void execute(int cycle_half) {
    if (cycle_half == 2) {
        update_fu_status(K0, 0);
        update_fu_status(K1, 1);
        update_fu_status(K2, 2);
    }
}

void update_fu_status(int k, int type) {
    for (int i = 0; i < k; i++) {
        if (scoreboard->fu[type][i].busy && !scoreboard->fu[type][i].life) {
            scoreboard->fu[type][i].life++;
        } else if (scoreboard->fu[type][i].busy && scoreboard->fu[type][i].life) {
            for (int j = 0; j < R; j++) {
                if (!CDB[j].busy) {
                    CDB[j].busy = 1;
                    CDB[j].tag = scoreboard->fu[type][i].tag;
                    CDB[j].value = scoreboard->fu[type][i].value;
                    CDB[j].reg = scoreboard->fu[type][i].reg;
                    scoreboard->fu[type][i].life = 0;
                    scoreboard->fu[type][i].busy = 0;
                    sched_q[scoreboard->fu[type][i].inst_id].busy = 0;
                    break;
                }
            }
        }
    }
}

void update_reg_file(int cycle_half) {
    if (cycle_half == 1) {
        for (int j = 0; j < R; j++) {
            if (CDB[j].busy && CDB[j].tag == registers[CDB[j].reg].tag) {
                registers[CDB[j].reg].ready = 1; 
                registers[CDB[j].reg].value = CDB[j].value;
                CDB[j].busy = 0;
            }
        } 
    }

}



