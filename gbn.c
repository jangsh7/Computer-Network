#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#pragma warning(disable:4996)

#define LOSS_PROB 0.2       // Probability that a packet is lost in the network
#define CORRUPT_PROB 0.1    // Probability that a packet is corrupted in the network
#define SIM_MESSAGE_MAX 50  // Number of messages to simulate from layer 5

#define BIDIRECTIONAL 1     // Enable bi-directional communication
#define TIME_AVERAGE 5      // Average time interval between messages
#define TIMERINCREMENT 30.0 // Time interval for retransmission timer

#define TIMER_INTERRUPT 0  
#define FROM_LAYER5 1
#define FROM_LAYER3 2

#define OFF 0
#define ON 1
#define A 0
#define B 1

#define mmm RAND_MAX

#define WINDOW_SIZE 8
#define TIMEOUT 30.0

struct msg {                // Message from layer 5 (application)
    char data[20];
};

struct pkt {                // Network layer packet with basic header and payload
    int seqnum;
    int acknum;
    int checksum;
    char payload[20];
};

struct event {              // Event in the event-driven simulator
    float evtime;
    int evtype;
    int eventity;
    struct pkt packet;
    struct event* prev;
    struct event* next;
};

struct event* evlist = NULL;
int TRACE = 3;
int nsim = 0;
int nsimmax = 0;
float time = 0.000;
float lossprob;
float corruptprob;
float lambda;
int ntolayer3 = 0;          // Total packets sent to layer 3
int nlost = 0;              // Total lost packets
int ncorrupt = 0;           // Total corrupted packets

int bytes_delivered_to_layer5 = 0;  // Number of bytes delivered to application

// A-side state
int A_base = 0;
int A_next_seqnum = 0;
struct pkt A_window[WINDOW_SIZE];
int A_timer_on = 0;

// B-side expected seqnum
int B_expected_seqnum = 0;

// 
int resend_count = 0;
int ack_count = 0;
int next_seqnum = 0;
enum A_State { A_IDLE, A_SENDING, A_WAIT_ACK, A_TIMEOUT };
enum A_State A_state = A_IDLE;

#define SEQ_SPACE 32  // 시퀀스 넘버 공간 크기
#define WINDOW_SIZE 8
#define TIMEOUT 30.0

int base = 0;
int next_seq = 0;

struct pkt window[SEQ_SPACE];  // 송신 윈도우용 버퍼
int acked[SEQ_SPACE];          // ACK 수신 여부 추적

int total_sent = 0;
int total_retransmissions = 0;
int total_acks_received = 0;
int total_delivered_to_layer5 = 0;


void tolayer3(int AorB, struct pkt packet);
void tolayer5(int AorB, char datasent[20]);
void starttimer(int AorB, float increment);
void stoptimer(int AorB);
void generate_next_arrival(void);
void insertevent(struct event* p);
float jimsrand(void);
void init(void);
void printevlist(void);

/* 추가 */
int compute_checksum(struct pkt* packet) {
    int sum = packet->seqnum + packet->acknum;
    for (int i = 0; i < 20; i++) {
        sum += (unsigned char)packet->payload[i];
    }
    return sum;
}

void send_ack(int acknum) {
    struct pkt ack_pkt;
    ack_pkt.seqnum = 0;
    ack_pkt.acknum = acknum;
    memset(ack_pkt.payload, 0, 20);
    ack_pkt.checksum = compute_checksum(&ack_pkt);
    tolayer3(B, ack_pkt);
}





//void print_summary_stats(float sim_time) {
//    FILE* fp = fopen("stat_log.txt", "w");
//    if (fp) {
//        fprintf(fp, "====== Simulation Summary ======\n");
//        fprintf(fp, "Total packets sent to layer3: %d\n", ntolayer3);
//        fprintf(fp, "Total lost packets: %d\n", nlost);
//        fprintf(fp, "Total corrupted packets: %d\n", ncorrupt);
//        fprintf(fp, "Total retransmissions: %d\n", resend_count);
//        fprintf(fp, "Total ACKs received: %d\n", ack_count);
//        fprintf(fp, "Total bytes delivered to layer5: %d\n", bytes_delivered_to_layer5);
//        fprintf(fp, "Throughput: %.2f bytes/sec\n", bytes_delivered_to_layer5 / sim_time);
//        fclose(fp);
//    }
//}

void print_summary_stats() {
    FILE* fp = fopen("stat_log.txt", "w");
    if (fp == NULL) return;

    fprintf(fp, "Total packets sent: %d\n", total_sent);
    fprintf(fp, "Total packets retransmitted: %d\n", total_retransmissions);
    fprintf(fp, "Total ACKs received: %d\n", total_acks_received);
    fprintf(fp, "Total packets delivered to layer 5: %d\n", total_delivered_to_layer5);

    double throughput = (double)total_delivered_to_layer5 / (total_sent + total_retransmissions);
    fprintf(fp, "Throughput: %.3f\n", throughput);

    fclose(fp);
}


/********* Protocol-side functions (to be implemented by student) **************/
void A_output(struct msg message) {
    if ((next_seq - base + SEQ_SPACE) % SEQ_SPACE < WINDOW_SIZE) {
        A_state = A_SENDING;

        struct pkt p;
        p.seqnum = next_seq;
        p.acknum = 999;
        strncpy(p.payload, message.data, 20);
        p.checksum = compute_checksum(&p);

        window[next_seq] = p;
        acked[next_seq] = 0;

        tolayer3(0, p);
        starttimer(0, TIMEOUT); next_seq = (next_seq + 1) % SEQ_SPACE;
    }
}

void A_input(struct pkt p) {
    if (compute_checksum(&p) != p.checksum) return;

    int acknum = p.acknum;
    ack_count++; A_state = A_WAIT_ACK;

    if ((base <= acknum && acknum < base + WINDOW_SIZE) ||
        (base + WINDOW_SIZE >= SEQ_SPACE && acknum < (base + WINDOW_SIZE) % SEQ_SPACE)) {

        acked[acknum] = 1;
        stoptimer(0);

        while (acked[base]) {
            acked[base] = 0;
            base = (base + 1) % SEQ_SPACE;
        }A_state = A_IDLE;
    }
}

void A_timerinterrupt() {
    resend_count++; starttimer(A, TIMEOUT);

    for (int i = base; i < next_seqnum; i++) {
        tolayer3(A, A_window[i % WINDOW_SIZE]);
    }
}

void A_init() {
    for (int i = 0; i < SEQ_SPACE; i++) acked[i] = 0;
    base = 0;
    next_seq = 0;
    A_state = A_IDLE;// 종료 시 호출될 함수
}


void B_output(struct msg message) {
    // GBN 단방향 기준 불필요
}

void B_input(struct pkt p) {
    if (compute_checksum(&p) != p.checksum) return;

    struct pkt ack;
    ack.acknum = p.seqnum;
    ack.seqnum = 0;
    memset(ack.payload, 0, sizeof(ack.payload));
    ack.checksum = compute_checksum(&ack);

    tolayer3(1, ack); tolayer5(1, p.payload);
}

void B_timerinterrupt() {
    // GBN 단방향 기준 불필요
}

void B_init() {
    // SR용 수신 창 없음
}




/********************* SIMULATOR CODE *********************/
int main() {
    struct event* eventptr;
    struct msg msg2give;
    struct pkt pkt2give;
    int i, j;

    init();         // Initialize simulation
    A_init();       // Initialize A
    B_init();       // Initialize B

    while (1) {
        eventptr = evlist;          // Fetch next event from event list
        if (eventptr == NULL) break;
        evlist = evlist->next;
        if (evlist != NULL) evlist->prev = NULL;
        if (TRACE >= 2) {
            printf("\nEVENT time: %f,", eventptr->evtime);
            printf("  type: %d", eventptr->evtype);
            if (eventptr->evtype == 0)
                printf(", timerinterrupt  ");
            else if (eventptr->evtype == 1)
                printf(", fromlayer5 ");
            else
                printf(", fromlayer3 ");
            printf(" entity: %d\n", eventptr->eventity);
        }
        time = eventptr->evtime;    // Advance simulation time

        if (nsim == nsimmax) break;

        // Handle different types of events
        if (eventptr->evtype == FROM_LAYER5) {
            generate_next_arrival();    // Schedule next message arrival
            j = nsim % 26;
            for (i = 0; i < 20; i++) msg2give.data[i] = 'a' + j;
            if (TRACE > 2) {
                printf("          MAINLOOP: data given to student: ");
                for (i = 0; i < 20; i++)
                    printf("%c", msg2give.data[i]);
                printf("\n");
            }
            nsim++;
            if (eventptr->eventity == A)
                A_output(msg2give);     // Call A's output handler
            else
                B_output(msg2give);     // Call B's output handler
        }
        else if (eventptr->evtype == FROM_LAYER3) {
            pkt2give = eventptr->packet;
            if (eventptr->eventity == A)
                A_input(pkt2give);      // Call A's input handler
            else
                B_input(pkt2give);      // Call B's input handler
        }
        else if (eventptr->evtype == TIMER_INTERRUPT) {
            if (eventptr->eventity == A)
                A_timerinterrupt();
            else
                B_timerinterrupt();
        }
        else {
            printf("INTERNAL PANIC: unknown event type\n");
        }
        free(eventptr);                 // Clean up processed event
    }

    printf("Simulator terminated at time %f\n after sending %d msgs from layer5\n", time, nsim);
    printf("Total bytes received at layer5: %d\n", bytes_delivered_to_layer5);
    printf("Throughput: %.2f bytes/sec\n", bytes_delivered_to_layer5 / time);
    return 0;
}

void init(void) {
    int i;
    float sum = 0.0, avg;

    printf("-----  Sliding Window Network Simulator -------- \n\n");
    printf("Drop Rate: %.2f, Corrupt Rate: %.2f (fixed)\n", LOSS_PROB, CORRUPT_PROB);

    lossprob = LOSS_PROB;
    corruptprob = CORRUPT_PROB;
    nsimmax = SIM_MESSAGE_MAX;
    lambda = TIME_AVERAGE;

    srand(9999);
    sum = 0.0;                /* test random number generator for students */
    for (i = 0; i < 1000; i++)
        sum = sum + jimsrand();    /* jimsrand() should be uniform in [0,1] */
    avg = sum / 1000.0;
    if (avg < 0.25 || avg > 0.75) {
        printf("It is likely that random number generation on your machine\n");
        printf("is different from what this emulator expects.  Please take\n");
        printf("a look at the routine jimsrand() in the emulator code. Sorry. \n");
        exit(0);
    }

    ntolayer3 = 0;
    nlost = 0;
    ncorrupt = 0;
    time = 0.0;
    generate_next_arrival();
}

float jimsrand() {
    float x;                   /* individual students may need to change mmm */
    x = ((float)rand()) / mmm;           /* x should be uniform in [0,1] */
    return(x);
}

void generate_next_arrival(void) {
    double x;
    struct event* evptr = (struct event*)malloc(sizeof(struct event));

    if (TRACE > 2)
        printf("          GENERATE NEXT ARRIVAL: creating new arrival\n");

    x = lambda * jimsrand() * 2;
    evptr->evtime = time + x;   // Schedule the arrival time
    evptr->evtype = FROM_LAYER5;
    evptr->eventity = (BIDIRECTIONAL && (jimsrand() > 0.5)) ? B : A;
    insertevent(evptr);         // Insert into event list
}

void insertevent(struct event* p) {
    struct event* q = evlist;
    struct event* qold = NULL;
    if (TRACE > 3) {
        printf("            INSERTEVENT: time is %lf\n", time);
        printf("            INSERTEVENT: future time will be %lf\n", p->evtime);
    }

    if (q == NULL) {   /*list is empty*/
        evlist = p;
        p->next = NULL;
        p->prev = NULL;
    }
    else {
        while (q != NULL && p->evtime > q->evtime) {
            qold = q;
            q = q->next;
        }
        if (q == NULL) {  /* end of list */
            qold->next = p;
            p->prev = qold;
            p->next = NULL;
        }
        else if (q == evlist) { /* front of list */
            p->next = evlist;
            p->prev = NULL;
            p->next->prev = p;
            evlist = p;
        }
        else {  /* middle of list */
            p->next = q;
            p->prev = q->prev;
            q->prev->next = p;
            q->prev = p;
        }
    }
}


/********************** Student-callable ROUTINES ***********************/
/* called by students routine to cancel a previously-started timer */
void stoptimer(int AorB) {
    struct event* q;

    if (TRACE > 2)
        printf("          STOP TIMER: stopping timer at %f\n", time);

    // Search for the timer event in the event list    
    for (q = evlist; q != NULL; q = q->next) {
        if ((q->evtype == TIMER_INTERRUPT) && (q->eventity == AorB)) {

            // Remove the event from the linked list
            if (q->next == NULL && q->prev == NULL)
                evlist = NULL;          // Only event in list
            else if (q->next == NULL)
                q->prev->next = NULL;   // Last event
            else if (q == evlist) {
                q->next->prev = NULL;   // First event
                evlist = q->next;
            }
            else {
                q->next->prev = q->prev;    // Middle of list
                q->prev->next = q->next;
            }
            free(q);    // Free memory
            return;
        }
    }
    // Timer was not found — may not have been started
    printf("Warning: unable to cancel your timer. It wasn't running.\n");
}

void starttimer(int AorB, float increment) {
    struct event* q;
    struct event* evptr;

    if (TRACE > 2)
        printf("          START TIMER: starting timer at %f\n", time);

    // Check if a timer is already running for this seqnum
    for (q = evlist; q != NULL; q = q->next) {
        if ((q->evtype == TIMER_INTERRUPT) && (q->eventity == AorB)) {
            printf("Warning: attempt to start a timer that is already started\n");
            return;
        }
    }

    // Create and initialize the timer event
    evptr = (struct event*)malloc(sizeof(struct event));
    evptr->evtime = time + increment;   // When the timer should expire
    evptr->evtype = TIMER_INTERRUPT;
    evptr->eventity = AorB;
    insertevent(evptr);     // Add to event list
}

void tolayer3(int AorB, struct pkt pkt) {
    struct event* evptr;
    struct event* q;
    float lastime;
    int i;
    float x;

    ntolayer3++;    // Track total packets sent into the network

    /* simulate losses: */
    if (jimsrand() < lossprob) {
        nlost++;
        if (TRACE > 0)
            printf("          TOLAYER3: packet %d being lost\n", pkt.seqnum);
        return;
    }

    // Create a new arrival event on the receiver's side
    evptr = (struct event*)malloc(sizeof(struct event));
    evptr->evtype = FROM_LAYER3;
    evptr->eventity = (AorB + 1) % 2;  // Deliver to the other entity (A <-> B)

    evptr->packet = pkt;  // Copy entire packet

    if (TRACE > 2) {
        printf("          TOLAYER3: check: %d ", evptr->packet.checksum);
        for (i = 0; i < 20; i++)
            printf("%c", evptr->packet.payload[i]);
        printf("\n");
    }

    // Schedule arrival time (simulate network delay)
    lastime = time;
    for (q = evlist; q != NULL; q = q->next) {
        if (q->evtype == FROM_LAYER3 && q->eventity == evptr->eventity) {
            lastime = q->evtime;    // Delay stacking with existing events
        }
    }
    evptr->evtime = lastime + 1 + 9 * jimsrand();   // Add random delay

    /* simulate corruption: */
    if (jimsrand() < corruptprob) {
        ncorrupt++;
        x = jimsrand();
        if (x < 0.75)
            evptr->packet.payload[0] = 'Z';  // Corrupt payload
        else if (x < 0.875)
            evptr->packet.seqnum = 999999;   // Corrupt sequence number
        else
            evptr->packet.acknum = 999999;   // Corrupt ACK number
        if (TRACE > 0)
            printf("          TOLAYER3: packet %d being corrupted\n", pkt.seqnum);
    }

    if (TRACE > 2) {
        printf("          TOLAYER3: scheduling arrival on other side at time %f\n", evptr->evtime);
    }

    insertevent(evptr); // Add to simulator's event queue
}

void tolayer5(int AorB, char datasent[20]) {
    int i;
    bytes_delivered_to_layer5 += 20; // Update total bytes delivered

    if (TRACE > 2) {
        printf("          TOLAYER5: data received: ");
        for (i = 0; i < 20; i++)
            printf("%c", datasent[i]);
        printf("\n");
    }
}

void printevlist() {
    struct event* q;
    int i;
    printf("--------------\nEvent List Follows:\n");
    for (q = evlist; q != NULL; q = q->next) {
        printf("Event time: %f, type: %d entity: %d\n", q->evtime, q->evtype, q->eventity);
    }
    printf("--------------\n");
}