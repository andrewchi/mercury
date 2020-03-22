/*
 * output.c
 */

#include <stdlib.h>
#include <sys/time.h>
#include <string.h>
#include "output.h"
#include "pcap_file_io.h"  // for write_pcap_file_header()
#include "utils.h"

extern struct thread_queues t_queues;  // defined in mercury.c
extern int sig_stop_output;            // defined in mercury.h

#define output_file_needs_rotation(ojf) (--((ojf)->record_countdown) == 0)

void init_t_queues(struct thread_queues *tqs, int n) {
    tqs->qnum = n;
    tqs->queue = (struct ll_queue *)calloc(n, sizeof(struct ll_queue));

    if (tqs->queue == NULL) {
        fprintf(stderr, "Failed to allocate memory for thread queues\n");
        exit(255);
    }

    for (int i = 0; i < n; i++) {
        tqs->queue[i].qnum = i; /* only needed for debug output */
        tqs->queue[i].ridx = 0;
        tqs->queue[i].widx = 0;

        for (int j = 0; j < LLQ_DEPTH; j++) {
            tqs->queue[i].msgs[j].used = 0;
        }
    }
}


void destroy_thread_queues(struct thread_queues *tqs) {
    free(tqs->queue);
    tqs->queue = NULL;
    tqs->qnum = 0;
}


int time_less(struct timespec *tsl, struct timespec *tsr) {

    if ((tsl->tv_sec < tsr->tv_sec) || ((tsl->tv_sec == tsr->tv_sec) && (tsl->tv_nsec < tsr->tv_nsec))) {
        return 1;
    } else {
        return 0;
    }
}

int queue_less(int ql, int qr, struct tourn_tree *t_tree) {

    /* returns 1 if the time of ql < qr and 0 otherwise
     * Also sets t_tree->stalled = 1 if needed.
     *
     * WARNING: This function is NOT thread safe!
     *
     * Meaning the access to the 'used' member in the queue
     * struct happens and then later the access to the
     * struct timespec happens.
     * This function must be called by the output thread
     * and ONLY the output thread because if
     * queues are changed while this function is going
     * shit will hit the fan!
     */

    int ql_used = 0; /* The (l)eft queue in the tree */
    int qr_used = 0; /* The (r)ight queue in the tree */

    /* check for a queue stall before we return anything otherwise
     * we could short-circuit logic before realizing one of the
     * queues was stalled
     */
    if ((ql >= 0) && (ql < t_queues.qnum)) {
        ql_used = t_queues.queue[ql].msgs[t_queues.queue[ql].ridx].used;
        if (ql_used == 0) {
            t_tree->stalled = 1;
        }
    }
    if ((qr >= 0) && (qr < t_queues.qnum)) {
        qr_used = t_queues.queue[qr].msgs[t_queues.queue[qr].ridx].used;
        if (qr_used == 0) {
            t_tree->stalled = 1;
        }
    }

    /* If the queue numbers here are -1 that means we've spilled
     * over into the portion of the tournament tree that isn't
     * populated by queues because the number of queues wasn't a
     * power-of-two
     *
     * Don't blindly combine this into the above statements as an else
     * without realising that both qr and ql must be checked for
     * a stall before any return is done
     */
    if (ql == -1) {
        return 0;
    } else if (qr == -1) {
        return 1;
    }

    /* The t_tree is built as though the number of queues is
     * a power-of-two however it doesn't actually have to be
     * that way so if the computed queue number spills over past
     * the actual number of queues we just fill the tree with -1
     * to indicate that portion of the tree shouldn't be use
     * in the tournament (and any real queue compared to a -1 queue
     * automatically "wins").
     */
    if (ql >= t_queues.qnum) {
        return 0;
    } else if (qr >= t_queues.qnum) {
        return 1;
    }

    /* This is where we do the actual less comparison */
    if (ql_used == 0) {
        return 0;
    } else if (qr_used == 0) {
        return 1;
    } else {
        struct timespec *tsl = &(t_queues.queue[ql].msgs[t_queues.queue[ql].ridx].ts);
        struct timespec *tsr = &(t_queues.queue[qr].msgs[t_queues.queue[qr].ridx].ts);

        return time_less(tsl, tsr);
    }
}


int lesser_queue(int ql, int qr, struct tourn_tree *t_tree) {

    if (queue_less(ql, qr, t_tree) == 1) {
        return ql;
    } else {
        return qr;
    }
}


void run_tourn_for_queue(struct tourn_tree *t_tree, int q) {

    /*
     * The leaf index in the tree for a particular queue
     * is the queue's index in the tree minus 1 (or 2) divided by 2
     * however we don't bother to store the bottem-most layer in the
     * the tree and also, by clearing the least significant bit in
     * the q number we can reduce the minus 1 or 2 to just minus 1.
     */

    int ql = (q % 2 == 0)? q : q - 1; /* the even q is (l)eft */
    int qr = ql + 1;                  /* the odd q is (r)ight */
    int lidx = ((ql + t_tree->qp2) - 1) / 2;

    t_tree->tree[lidx] = lesser_queue(ql, qr, t_tree);

    /* This "walks" back up the tree to the root node (0) */
    while (lidx > 0) {
        lidx = (lidx - 1) / 2; /* Up up a level in the tree */
        ql = t_tree->tree[(lidx * 2) + 1]; /* (l)eft child queue */
        qr = t_tree->tree[(lidx * 2) + 2]; /* (r)ight child queue */

        /* Run the tournament between ql and qr */
        t_tree->tree[lidx] = lesser_queue(ql, qr, t_tree);
    }
}


void debug_print_tour_tree(struct tourn_tree *t_tree) {

    fprintf(stderr, "Tourn Tree size: %d\n", (t_tree->qp2 - 1));
    int i = 0;
    int l = 2;
    while (i < (t_tree->qp2 - 1)) {
        for (; i < l - 1; i++) {
            fprintf(stderr, "%d ", t_tree->tree[i]);
        }
        fprintf(stderr, "\n");
        l *= 2;
    }

    fprintf(stderr, "Ready queues:\n");
    for (int q = 0; q < t_tree->qnum; q++) {
        if (t_queues.queue[q].msgs[t_queues.queue[q].ridx].used == 1) {
            fprintf(stderr, "%d ", q);
        }
    }
    fprintf(stderr, "\n");
}

enum status output_file_rotate(struct output_file *ojf) {
    char outfile[MAX_FILENAME];

    if (ojf->file) {
        // printf("rotating output file\n");

        if (fclose(ojf->file) != 0) {
            perror("could not close json file");
        }
    }

    if (ojf->max_records) {
        /*
         * create filename that includes sequence number and date/timestamp
         */
        char file_num[MAX_HEX];
        snprintf(file_num, MAX_HEX, "%x", ojf->file_num++);
        enum status status = filename_append(outfile, ojf->outfile_name, "-", file_num);
        if (status) {
            return status;
        }

        char time_str[128];
        struct timeval now;
        gettimeofday(&now, NULL);
        strftime(time_str, sizeof(time_str) - 1, "%Y%m%d%H%M%S", localtime(&now.tv_sec));
        status = filename_append(outfile, outfile, "-", time_str);
        if (status) {
            return status;
        }
    } else {
        ojf->max_records = UINT64_MAX;
        strncpy(outfile, ojf->outfile_name, MAX_FILENAME - 1);
    }

    ojf->file = fopen(outfile, ojf->mode);
    if (ojf->file == NULL) {
        perror("error: could not open fingerprint output file");
        return status_err;
    }
    if (ojf->type == pcap) {
        enum status status = write_pcap_file_header(ojf->file);
        if (status) {
            perror("error: could not write pcap file header");
            return status_err;
        }
    }

    ojf->record_countdown = ojf->max_records;

    return status_ok;
}

void *output_thread_func(void *arg) {

    struct output_file *out_ctx = (struct output_file *)arg;

    int err;
    err = pthread_mutex_lock(&(out_ctx->t_output_m));
    if (err != 0) {
        fprintf(stderr, "%s: error locking output start mutex for stats thread\n", strerror(err));
        exit(255);
    }
    while (out_ctx->t_output_p != 1) {
        err = pthread_cond_wait(&(out_ctx->t_output_c), &(out_ctx->t_output_m));
        if (err != 0) {
            fprintf(stderr, "%s: error waiting on output start condition for stats thread\n", strerror(err));
            exit(255);
        }
    }
    err = pthread_mutex_unlock(&(out_ctx->t_output_m));
    if (err != 0) {
        fprintf(stderr, "%s: error unlocking output start mutex for stats thread\n", strerror(err));
        exit(255);
    }

    // note: we wait until we get an output start condition before we
    // open any output files, so that drop_privileges() can be called
    // before file creation
    output_file_rotate(out_ctx);

    /* This output thread uses a "tournament tree" algorithm
     * to perform a k-way merge of the lockless queues.
     *
     * https://en.wikipedia.org/wiki/Tournament_sort
     * https://www.geeksforgeeks.org/tournament-tree-and-binary-heap/
     * https://en.wikipedia.org/wiki/Priority_queue
     *
     * The actual algorithm is virtually identical to a priority queue
     * with the caveat that instead of swapping elements in an array
     * the priority queue just tracks a tree if of the "winning" queue
     * index.  In this algorithm "winning" is the oldest message.
     *
     * This algorithm is very efficient because it leaves messages in
     * the lockless queue until they are ready to be sent to output
     * intsead of making copies of messages and throwing them in a
     * priority queue.
     *
     * One "gotcha" about the usual k-way merge with a tournament tree
     * is that we're reading messages out of the lockless queues in
     * real-time as the queues are being filled.  This means not all
     * queues will always have a message in them which means we can't
     * really run the tournament properly because we don't know the
     * timestamp of the next message that queue will have when a
     * message finally arrives.
     *
     * To avoid things getting out-of-order the output thread won't
     * run a tournament until either 1) all queues have a message in
     * them, or 2) one of the queues has a message older than
     * LLQ_MAX_AGE (5 seconds by default).
     *
     * This means that as long as no queue pauses for more than 5
     * seconds the k-way merge will be perfectly in-order.  If a queue
     * does pause for more than 5 seconds only messages older than 5
     * seconds will be flushed.
     *
     * The other big assumetion is that each lockless queue is in
     * perfect order.  Testing shows that rarely, packets can be
     * out-of-order by a few microseconds in a lockless queue.  This
     * may be the fault of tiny clock abnormalities, could be machine
     * dependant, or ethernet card dependant.  The exact situations
     * where packets can be recieved out of cronological order aren't
     * known (to me anyways).
     */

    struct tourn_tree t_tree;
    t_tree.qnum = t_queues.qnum;
    t_tree.qp2 = 2; /* This is the smallest power of 2 >= the number of queues */
    while (t_tree.qp2 < t_tree.qnum) {
        t_tree.qp2 *= 2;
    }
    t_tree.tree = (int *)calloc(t_tree.qp2 - 1, sizeof(int)); /* The tournament needs qp2 - 1 nodes */
    if (t_tree.tree == NULL) {
        fprintf(stderr, "Failed to allocate enough memory for the tournament tree\n");
        exit(255);
    }

    int all_output_flushed = 0;
    while (all_output_flushed == 0) {

        /* run the tournament for every queue */
        t_tree.stalled = 0;
        /* Every other works here because the tournament
         * works on pairs: {0,1}, {2,3}, {3,4}, etc.
         * Passing a q from either pair runs the tournament
         * for the pair.
         */
        for (int q = 0; q < t_tree.qp2; q += 2) {
            run_tourn_for_queue(&t_tree, q);
        }

        /* This loop runs the tournament as long as the tree
         * isn't "stalled".  A stalled tree means at least
         * one of the lockless queues is currenty empty.
         */

        int wq; /* winning queue */
        while (t_tree.stalled == 0) {
            wq = t_tree.tree[0]; /* the root node is always the winning queue */

            struct llq_msg *wmsg = &(t_queues.queue[wq].msgs[t_queues.queue[wq].ridx]);
            if (wmsg->used == 1) {
                fwrite(wmsg->buf, wmsg->len, 1, out_ctx->file);

                /* A full memory barrier prevents the following flag (un)set from happening too soon */
                __sync_synchronize();
                wmsg->used = 0;

                /* Handle rotating file if needed */
                if (output_file_needs_rotation(out_ctx)) {
                    output_file_rotate(out_ctx);
                }

                t_queues.queue[wq].ridx = (t_queues.queue[wq].ridx + 1) % LLQ_DEPTH;

                run_tourn_for_queue(&t_tree, wq);
            }
            else {
                break;
            }

        }

        /* The tree is now stalled because a queue has been emptied
         * Now we must remove messages as long as they are "too old"
         */
        struct timespec old_ts;
        if (clock_gettime(CLOCK_REALTIME, &old_ts) != 0) {
            perror("Unable to get current time");
        }

        /* This is the time we compare against to flush */
        old_ts.tv_sec -= LLQ_MAX_AGE;

        /* This loop runs the tournament even though the tree is stalled
         * but only pull messages out of queues that are older than
         * LLQ_MAX_AGE (currently set to 5 seconds).
         */

        int old_done = 0;
        while (old_done == 0) {
            wq = t_tree.tree[0];

            struct llq_msg *wmsg = &(t_queues.queue[wq].msgs[t_queues.queue[wq].ridx]);
            if (wmsg->used == 0) {
                /* Even the top queue has nothing so we can just stop now */
                old_done = 1;

                /* This is how we detect no more output is coming */
                if (sig_stop_output != 0) {
                    all_output_flushed = 1;
                }

                break;
            } else if (time_less(&(wmsg->ts), &old_ts) == 1) {
                //fprintf(stderr, "DEBUG: writing old message from queue %d\n", wq);
                fwrite(wmsg->buf, wmsg->len, 1, out_ctx->file);

                /* A full memory barrier prevents the following flag (un)set from happening too soon */
                __sync_synchronize();
                wmsg->used = 0;

                /* Handle rotating file if needed */
                if (output_file_needs_rotation(out_ctx)) {
                    output_file_rotate(out_ctx);
                }

                t_queues.queue[wq].ridx = (t_queues.queue[wq].ridx + 1) % LLQ_DEPTH;

                run_tourn_for_queue(&t_tree, wq);
            } else {
                old_done = 1;
            }
        }

        /* This sleep slows us down so we don't spin the CPU.
         * We probably could afford to call fflush() here
         * the first time instead of sleeping and only sleep
         * if we really aren't recieving any messages on
         * any queues.
         */
        struct timespec sleep_ts;
        sleep_ts.tv_sec = 0;
        sleep_ts.tv_nsec = 1000;
        nanosleep(&sleep_ts, NULL);
    } /* End all_output_flushed == 0 meaning we got a signal to stop */

    if (t_tree.tree) {
        free(t_tree.tree);
    }
    if (fclose(out_ctx->file) != 0) {
        perror("could not close json file");
    }

    return NULL;
}


int output_thread_init(pthread_t &output_thread, struct output_file &out_ctx, const struct mercury_config &cfg) {

    /* make the thread queues */
    init_t_queues(&t_queues, cfg.num_threads);
    out_ctx.qs = &t_queues;

    /* init the output context */
    if (pthread_cond_init(&(out_ctx.t_output_c), NULL) != 0) {
        perror("Unabe to initialize output condition");
        return -1;
    }
    if (pthread_mutex_init(&(out_ctx.t_output_m), NULL) != 0) {
        perror("Unabe to initialize output mutex");
        return -1;
    }
    out_ctx.t_output_p = 0;

    out_ctx.file = NULL;
    out_ctx.max_records = cfg.rotate;
    out_ctx.record_countdown = 0;
    if (cfg.fingerprint_filename) {
        out_ctx.outfile_name = cfg.fingerprint_filename;
        out_ctx.type = json;

    } else if (cfg.write_filename) {
        out_ctx.outfile_name = cfg.write_filename;
        out_ctx.type = pcap;
    } else {
        out_ctx.type = unknown;
    }
    out_ctx.file_num = 0;
    out_ctx.mode = cfg.mode;

    //fprintf(stderr, "DEBUG: fingerprint filename: %s\n", cfg.fingerprint_filename);
    //fprintf(stderr, "DEBUG: max records: %ld\n", out_ctx.out_jf.max_records);

    /* Start the output thread */
    int err = pthread_create(&output_thread, NULL, output_thread_func, &out_ctx);
    if (err != 0) {
        perror("error creating output thread");
        return -1;
    }
    return 0;
}
