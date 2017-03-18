#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>
#include <string.h>
#include <assert.h>


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
    int target_address;
    int instruction_address;
    int inst_id;
    int jump_cond;
    struct inst_status* status;
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
    struct instruction* inst;
};

struct inst_status {
    int inst_id;
    int fetch_cycle;
    int disp_cycle;
    int sched_cycle;
    int exec_cycle;
    int state_cycle;
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

typedef struct _proc_stats_t
{
        unsigned long retired_instruction;
        unsigned long Issue_instruction;
        unsigned long total_disp_size;
        float avg_inst_retired;
        float avg_inst_Issue;
        float avg_disp_size;
        unsigned long max_disp_size;
        unsigned long cycle_count;
} proc_stats_t;

void print_proc_settings();
void init_proc();
struct instruction* create_inst(uint64_t address, int op_type, int dest_reg, int src_reg1, int src_reg2);
void add_to_disp_q(struct instruction* inst);
void fetch_instructions();
void dispatch(int cycle_half);
void run_proc();
void clear_not_ready_buf();
void schedule(int cycle_half);
void execute(int cycle_half);
void update_reg_file(int cycle_half);
void update_fu_status(int k, int type);
void fetch_to_disp_trans();
int get_next_fired();

int R;
int F;
int K0;
int K1;
int K2;
int SCHED_Q_SIZE;
FILE* fp;
FILE* fp_write;
static const uint64_t DEFAULT_R = 2;
static const uint64_t DEFAULT_K0 = 3;
static const uint64_t DEFAULT_K1 = 2;
static const uint64_t DEFAULT_K2 = 1;
static const uint64_t DEFAULT_F = 4;
static const uint64_t REG_FILE_SIZE = 128;


struct dispatch_queue* disp_q;
struct schedule_queue* sched_q;
int* reserved_list;
int* inst_retire_list;
struct register_file* registers;
int next_tag;
int next_inst_id;
struct scoreboard* scoreboard;
struct common_data_bus* CDB;
proc_stats_t* p_stats;
int sched_q_size;
int cycle_count;
struct instruction** fetch_to_disp_buf;
int fetch_inst_size;



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
    fp_write = fopen("hmmer.100k.output", "w");

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
    print_proc_settings();
    run_proc();
    fclose(fp);
    fclose(fp_write);
}

void print_proc_settings() {
    fprintf(fp_write, "Processor Settings:\nR: %d\nk0: %d\nk1: %d\nk2: %d\nF: : %d\n", 
       R, K0, K1, K2, F);
    fprintf(fp_write, "INST\tFETCH\tDISP\tSCHED\tEXEC\tSTATE\n");
}

void run_proc() {
    while (!feof(fp) /*|| sched_q_size > 0 || disp_q->size > 0*/) {
        cycle_count++;
        // for (int cycle_half = 1; cycle_half < 3; cycle_half++) {
        //     update_reg_file(cycle_half);
        //     execute(cycle_half);
        //     schedule(cycle_half);
        //     dispatch(cycle_half);
        // }
        fetch_to_disp_trans();
        fetch_instructions();
    }
}

void init_proc() {
    SCHED_Q_SIZE = 2 * (K0 + K1 + K2); 
    next_tag = 0;
    next_inst_id = 0;
    sched_q_size = 0;
    fetch_inst_size = 0;
    if ((disp_q = calloc(1, sizeof(struct dispatch_queue))) == NULL) {
        perror("calloc");
        exit(EXIT_FAILURE);
    }
    if ((sched_q = calloc(SCHED_Q_SIZE, sizeof(struct schedule_queue))) == NULL) {
        perror("calloc");
        exit(EXIT_FAILURE);
    }

    if ((fetch_to_disp_buf = calloc(F, sizeof(struct instruction*))) == NULL) {
        perror("calloc");
        exit(EXIT_FAILURE);
    }

    if ((inst_retire_list = calloc(SCHED_Q_SIZE, sizeof(int))) == NULL) {
        perror("calloc");
        exit(EXIT_FAILURE);
    }
    if ((reserved_list = calloc(SCHED_Q_SIZE, sizeof(int))) == NULL) {
        perror("calloc");
        exit(EXIT_FAILURE);
    }

    if ((registers = calloc(REG_FILE_SIZE, sizeof(struct register_file))) == NULL) {
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
            struct instruction* inst = create_inst(address, op_type, dest_reg, src_reg1, src_reg2);
            inst->status->fetch_cycle = cycle_count;
            fetch_to_disp_buf[inst_count] = inst;
            assert(inst != NULL);
            fetch_inst_size = inst_count + 1;
            //add_to_disp_q(inst);
            //printf("read instruction %" PRIx64 "%d %d %d %d \n", address, op_type, dest_reg, src_reg1, src_reg2);
        }
        inst_count++;
    }
}

struct instruction* create_inst(uint64_t address, int op_type, int dest_reg, int src_reg1, int src_reg2) {
    struct instruction* inst = calloc(1, sizeof(struct instruction));
    inst->op_type = op_type;
    inst->dest = dest_reg;
    inst->src1 = src_reg1;
    inst->src2 = src_reg2;
    inst->instruction_address = address;
    inst->inst_id = next_inst_id++;
    inst->status = calloc(1, sizeof(struct inst_status));
    return inst;
}

void fetch_to_disp_trans() {
    for (int i = 0; i < fetch_inst_size; i++) {
        assert(fetch_to_disp_buf[i] != NULL);
        add_to_disp_q(fetch_to_disp_buf[i]);
        printf("instruction id = %d\n", disp_q->head[next_inst_id].inst->inst_id);    
    }
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
    inst->status->disp_cycle = cycle_count;
    disp_q->size++;
    entry->inst = inst;
    entry->next = NULL;
}


void dispatch(int cycle_half) {
    if (cycle_half == 1) {
        for(int i = 0; i < SCHED_Q_SIZE; i++) {
            if(!sched_q[i].busy && disp_q->size) {
                struct dispatch_entry* disp_entry = disp_q->head;
                disp_q->head = disp_q->head->next;
                disp_q->size--;
                sched_q_size++;
                sched_q[i].fu = disp_entry->inst->op_type; //dont' need this
                sched_q[i].dest_reg = disp_entry->inst->dest; //dont' need this
                disp_entry->inst->status->sched_cycle = cycle_count;
                sched_q[i].inst = disp_entry->inst;
                sched_q[i].busy = 1;
                reserved_list[i] = 1;
            }
        }

    } else {
        for (int i = 0; i < SCHED_Q_SIZE; i++) {
            if (reserved_list[i]) {
                if (sched_q[i].inst->src1 < 1) {
                    sched_q[i].src_reg1_ready = 1;
                } else if (registers[sched_q[i].inst->src1].ready) {
                    sched_q[i].src_reg1_value = registers[sched_q[i].inst->src1].value;
                    sched_q[i].src_reg1_ready = 1;
                } else {
                    sched_q[i].src_reg1_tag = registers[sched_q[i].inst->src1].tag;
                    sched_q[i].src_reg1_ready = 0;
                }

                if (sched_q[i].inst->src2 < 1) {
                    sched_q[i].src_reg2_ready = 1;
                } else if (registers[sched_q[i].inst->src2].ready) {
                    sched_q[i].src_reg2_value = registers[sched_q[i].inst->src2].value;
                    sched_q[i].src_reg2_ready = 1;
                } else {
                    sched_q[i].src_reg2_tag = registers[sched_q[i].inst->src2].tag;
                    sched_q[i].src_reg2_ready = 0;
                }

                if (sched_q[i].inst->dest > 0) {
                    registers[sched_q[i].inst->dest].tag = next_tag; 
                    sched_q[i].dest_reg_tag = next_tag++;
                    registers[sched_q[i].inst->dest].ready = 0;
                    
                }
                reserved_list[i] = 0;
            }
        }
    }
}

void schedule(int cycle_half) {
    if (cycle_half == 1) {
        int fire_offset = get_next_fired();
       
        while (fire_offset > 0) {
            int fu_type = sched_q[fire_offset].fu;
            if (fu_type < 0) {
                fu_type = 1;
            } 
            for (int j = 0; j < scoreboard->num_fus[fu_type]; j++) {
                if (fire_offset) {
                    scoreboard->fu[fu_type][j].busy = 1; 
                    scoreboard->fu[fu_type][j].life = 0;
                    scoreboard->fu[fu_type][j].inst_id = fire_offset;
                    scoreboard->fu[fu_type][j].reg = sched_q[fire_offset].dest_reg;
                    int tag = sched_q[fire_offset].dest_reg_tag;
                    scoreboard->fu[fu_type][j].tag = tag;
                    sched_q[fire_offset].inst->status->exec_cycle = cycle_count;
                    break;
                }
            }
            fire_offset = get_next_fired();
        } 
    } else {
        for (int i = 0; i < SCHED_Q_SIZE; i++) {
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
                    sched_q_size--;
                    sched_q[scoreboard->fu[type][i].inst_id].busy = 0;
                    sched_q[scoreboard->fu[type][i].inst_id].inst->status->state_cycle = cycle_count;
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

int get_next_fired() {
    int min_tag = next_tag;
    int min_offset = -1;
    for (int i = 0; i < SCHED_Q_SIZE; i++) {
        if (sched_q[i].src_reg1_ready 
            && sched_q[i].src_reg2_ready 
            && sched_q[i].dest_reg_tag <= min_tag) {

            min_tag = sched_q[i].dest_reg_tag;
            min_offset = i;
        }
    }
    return min_offset;
}

void print_statistics(proc_stats_t* p_stats, FILE* fp) {
        fprintf(fp,"Processor stats:\n");
        fprintf(fp,"Total instructions: %lu\n", p_stats->retired_instruction);
        fprintf(fp,"Avg Dispatch queue size: %f\n", p_stats->avg_disp_size);
        fprintf(fp,"Maximum Dispatch queue size: %lu\n", p_stats->max_disp_size);
        fprintf(fp,"Avg inst Issue per cycle: %f\n", p_stats->avg_inst_Issue);
        fprintf(fp,"Avg inst retired per cycle: %f\n", p_stats->avg_inst_retired);
        fprintf(fp,"Total run time (cycles): %lu\n", p_stats->cycle_count);
} 

// void complete_proc(proc_stats_t *p_stats, FILE* fp)
// {
//         p_stats->avg_disp_size = (double) p_stats->total_disp_size / p_stats->cycle_count;
//         p_stats->avg_inst_retired = (double) p_stats->retired_instruction / p_stats->cycle_count;
//         p_stats->avg_inst_Issue = (double) p_stats->Issue_instruction / p_stats->cycle_count;

//         fprintf(fp, "INST\tFETCH\tDISP\tSCHED\tEXEC\tSTATE\n");
//         for (int i = 0; i != procSim.Statitics.size(); ++i) {
//                 fprintf(fp, "%d\t", i + 1);
//                 for (int j = 0; j != 5; ++j)
//                         fprintf(fp, "%d\t", procSim.Statitics[i][j]);
//                 fprintf(fp, "\n");
//         }
//         fprintf(fp, "\n");
// }



