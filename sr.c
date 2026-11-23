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
#define SEQ_SPACE (2 * WINDOW_SIZE)
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

// 
int base_A = 0, nextseq_A = 0;
struct pkt window_A[SEQ_SPACE];
int acked_A[SEQ_SPACE];
int timer_A[SEQ_SPACE];

void tolayer3(int AorB, struct pkt packet);
void tolayer5(int AorB, char datasent[20]);
void starttimer(int AorB, float increment, int seqnum);
void stoptimer(int AorB, int seqnum);
void generate_next_arrival(void);
void insertevent(struct event* p);
float jimsrand(void);
void init(void);
void printevlist(void);

// 추가
int compute_checksum(struct pkt* p) {
    int sum = p->seqnum + p->acknum;
    for (int i = 0; i < 20; i++) sum += (unsigned char)p->payload[i];
    return sum;
}

/********* Protocol-side functions (to be implemented by student) **************/
void A_output(struct msg message) {
    if ((nextseq_A + SEQ_SPACE - base_A) % SEQ_SPACE < WINDOW_SIZE) {
        struct pkt p;
        p.seqnum = nextseq_A;
        p.acknum = 0;
        strncpy(p.payload, message.data, 20);
        p.checksum = compute_checksum(&p);

        window_A[nextseq_A] = p;
        acked_A[nextseq_A] = 0;
        timer_A[nextseq_A] = 1;

        tolayer3(0, p);
        starttimer(0, TIMEOUT, nextseq_A);

        nextseq_A = (nextseq_A + 1) % SEQ_SPACE;
    }
}

void A_input(struct pkt packet) {
    int checksum = compute_checksum(&packet);
    if (packet.checksum != checksum) return;

    int ack = packet.acknum;
    if ((base_A <= ack && ack < base_A + WINDOW_SIZE) ||
        (base_A + WINDOW_SIZE >= SEQ_SPACE && ack < (base_A + WINDOW_SIZE) % SEQ_SPACE)) {
        acked_A[ack] = 1;
        if (timer_A[ack]) {
            stoptimer(0, ack);
            timer_A[ack] = 0;
        }
        while (acked_A[base_A]) {
            base_A = (base_A + 1) % SEQ_SPACE;
        }
    }
}

void A_timerinterrupt(int seqnum) {
    if (!acked_A[seqnum]) {
        tolayer3(0, window_A[seqnum]);
        starttimer(0, TIMEOUT, seqnum);
    }
}

void A_init() {
    base_A = nextseq_A = 0;
    for (int i = 0; i < SEQ_SPACE; ++i) {
        acked_A[i] = 0;
        timer_A[i] = 0;
    }
}

int expected_B = 0;
struct pkt buffer_B[SEQ_SPACE];
int received_B[SEQ_SPACE];

void B_output(struct msg message) {

}

void B_input(struct pkt packet) {
    int checksum = compute_checksum(&packet);
    if (packet.checksum != checksum) return;

    int seq = packet.seqnum;
    if ((expected_B <= seq && seq < expected_B + WINDOW_SIZE) ||
        (expected_B + WINDOW_SIZE >= SEQ_SPACE && seq < (expected_B + WINDOW_SIZE) % SEQ_SPACE)) {

        if (!received_B[seq]) {
            received_B[seq] = 1;
            buffer_B[seq] = packet;
        }

        while (received_B[expected_B]) {
            tolayer5(1, buffer_B[expected_B].payload);
            received_B[expected_B] = 0;
            expected_B = (expected_B + 1) % SEQ_SPACE;
        }
    }

    struct pkt ack;
    ack.seqnum = 0;
    ack.acknum = seq;
    strncpy(ack.payload, "", 20);
    ack.checksum = compute_checksum(&ack);
    tolayer3(1, ack);
}

void B_timerinterrupt(int seqnum) {

}

void B_init() {
    expected_B = 0;
    for (int i = 0; i < SEQ_SPACE; ++i) received_B[i] = 0;
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
                A_timerinterrupt(eventptr->packet.seqnum);
            else
                B_timerinterrupt(eventptr->packet.seqnum);
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
void stoptimer(int AorB, int seqnum) {
    struct event* q;

    if (TRACE > 2)
        printf("          STOP TIMER: stopping timer at %f for sequence number %d\n", time, seqnum);

    // Search for the timer event in the event list    
    for (q = evlist; q != NULL; q = q->next) {
        if ((q->evtype == TIMER_INTERRUPT) && (q->eventity == AorB) && (q->packet.seqnum == seqnum)) {

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
            free(q); // Free memory
            return;
        }
    }
    // Timer was not found — may not have been started
    printf("Warning: unable to cancel your timer. It wasn't running.\n");
}

void starttimer(int AorB, float increment, int seqnum) {
    struct event* q;
    struct event* evptr;

    if (TRACE > 2)
        printf("          START TIMER: starting timer at %f for sequence number %d\n", time, seqnum);

    // Check if a timer is already running for this seqnum
    for (q = evlist; q != NULL; q = q->next) {
        if ((q->evtype == TIMER_INTERRUPT) && (q->eventity == AorB) && (q->packet.seqnum == seqnum)) {
            printf("Warning: attempt to start a timer that is already started\n");
            return;
        }
    }

    // Create and initialize the timer event
    evptr = (struct event*)malloc(sizeof(struct event));
    evptr->evtime = time + increment;   // When the timer should expire
    evptr->evtype = TIMER_INTERRUPT;
    evptr->eventity = AorB;
    evptr->packet.seqnum = seqnum;
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

    evptr->packet = pkt; // Copy entire packet

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
            evptr->packet.payload[0] = 'Z'; // Corrupt payload
        else if (x < 0.875)
            evptr->packet.seqnum = 999999;  // Corrupt sequence number
        else
            evptr->packet.acknum = 999999;  // Corrupt ACK number
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