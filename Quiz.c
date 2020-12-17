#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__,__LINE__),exit(EXIT_FAILURE))
#include <unistd.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>
#include <sys/wait.h>
#include <sys/stat.h>
#define QUESTIONS 10
#define MAX_PATH 300
#define DEFAULT_TIME 120
#define MAX_COMMAND_SIZE 100
#define LAST_REMIDER_TIME 30
#define QUIZFILE_EXT ".quiz"


typedef struct time_thread_args
{
    pthread_t tid;
    int t;
    int *is_time_up;
    pthread_mutex_t* mx_is_time_up;
    int *interrupt;
    pthread_mutex_t* mxinterrupt;
    pthread_t* scanf_tid;
} time_thread_args;

typedef struct d_thread_args
{
    pthread_t tid;
    char* d_path;
    char *path;
    int* Quitflag;
    pthread_mutex_t* mxQuitflag;
} d_thread_args;

typedef struct quit_thread_args
{
    pthread_t tid;
    int *Quitflag;
    pthread_mutex_t* mxQuitflag;
    pthread_t* scanf_tid;
    int *interrupt;
    pthread_mutex_t* mxinterrupt;
    sigset_t *pMask;
} quit_thread_args;

typedef struct scanf_thread_args
{
    pthread_t* tid;
    char *input;
} scanf_thread_args;


// mode validation
void read_arg_quizmode(int argc, char **argv, int *questions, int* time, char **path);
void read_arg_createmode(int argc, char **argv, char **path, char **dir_path);
int is_extension_valid(char *path);
void usage(char* pname);

// threads work
void* quit_thread_work(void* voidPtr);
void* time_thread_work(void *voidPtr);
void* d_thread_work(void* voidPtr);
void* scanf_thread_work(void* voidPtr);

// operations with file
void get_file_content(char *path, char **file_content);
int count_lines(char *file_content);
void add_question(char* path, char *word, char* translation);
int is_question_new(char *path, char *word);

// core functions 
void quiz_mode(int argc, char **argv);
void create_quiz_mode(int argc, char **argv);

// quiz mode
void insert_in_quiztab(char* file_content, int lines, char ***quiz_tab);
void test_user(char ***quiz_tab, int questions_in_file, int* correct, int n, int *is_time_up, pthread_mutex_t* mx_is_time_up, int *Quitflag, pthread_mutex_t* mx_quitflag, pthread_t* scanf_tid,int *interrupt, pthread_mutex_t* mx_interrupt);
void shuffle_questions(int *numbers, int questions_in_file);
void print_stats(int correct, int n);


void usage(char* pname)
{
    fprintf(stderr, "USAGE: %s\n", pname);
    fprintf(stderr, "-q (quiz mode)\n");
    fprintf(stderr, "\t-p PATH (.quiz)\tpath to quiz file\n");
    fprintf(stderr, "\tOptional:\n");
    fprintf(stderr, "\t-n [5-50] \tnumber of questions\n");
    fprintf(stderr, "\t-t [50-1000s] \ttime for quiz \n");
    fprintf(stderr, "-c (create quiz mode)\n");
    fprintf(stderr, "\t-p PATH (.quiz)\tpath to quiz file\n");
    fprintf(stderr, "\tOptional:\n");
    fprintf(stderr, "\t-d PATH \tdirectory of quiz files to add\n"); 
    exit(EXIT_FAILURE);
}

void read_arg_quizmode(int argc, char **argv, int *questions, int* time, char **path)
{
    // set default values
    *questions = QUESTIONS;
    *time = DEFAULT_TIME;
    
    // path is mandatory
    *path = malloc(sizeof(char)*MAX_PATH); 
    int is_p_set = 0;

    char c;
    optind = 2; // we ignore first parameter which is work_mode
    while ((c = getopt(argc, argv, "p:n:t:")) != -1)
    {
        switch (c)
        {
            case 'p':
                strcpy(*path,optarg);
                is_p_set = 1; 
                break;
            case 'n':
                *questions = atoi(optarg);
                if(*questions < 5 || *questions > 50) usage(argv[0]);
                break;
            case 't':
                *time = atoi(optarg);
                if(*time < 50 || *time > 1000) usage(argv[0]);
                break;
            default:
                usage(argv[0]);
                break;
        }
    }    
    if(!is_p_set)
    {
        printf("Path is mandatory!\n");
        usage(argv[0]);
    }   
    // file should have specific extension
    if(!is_extension_valid(*path)) 
    {
        printf("File should have %s extension\n", QUIZFILE_EXT);
        usage(argv[0]);
    } 
}

void read_arg_createmode(int argc, char **argv, char **path, char **dir_path)
{
    *path = malloc(sizeof(char)*MAX_PATH);
    *dir_path = malloc(sizeof(char)*MAX_PATH);
    int is_p_set = 0; 

    char c;
    optind = 2; // we ignore first one which is work_mode
    while ((c = getopt(argc, argv, "p:d:")) != -1)
    {
        switch (c)
        {
            case 'p':
                strcpy(*path,optarg);
                is_p_set = 1; 
                break;
            case 'd':
                strcpy(*dir_path,optarg);
                break;
            default:
                usage(argv[0]);
                break;
        }
    }    
    if(!is_p_set)
    {
        fprintf(stderr,"Path is mandatory!\n");
        usage(argv[0]);
    }
    if(!is_extension_valid(*path))
    {
        printf("File should have %s extension\n", QUIZFILE_EXT);
        usage(argv[0]);
    }    
}

int is_extension_valid(char *path)
{
    char *dot = strrchr(path,'.');
    if(dot == NULL) return 0;
    if(strcmp(dot,QUIZFILE_EXT) != 0) return 0;
    return 1;
}

void sethandler(void (*f)(int), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;
    if(-1 == sigaction(sigNo, &act, NULL)) ERR("sigacion");
}

void* quit_thread_work(void* voidPtr)
{
    quit_thread_args *args = voidPtr;
    int signo;
    for(;;)   
    {
        if(sigwait(args->pMask, &signo)) ERR("sigwait");
        if(signo == SIGINT || signo == SIGQUIT)
        {
            // set interruption flag
            pthread_mutex_lock(args->mxinterrupt);
            *args->interrupt = 1;
            pthread_mutex_unlock(args->mxinterrupt);

            // confirm quit
            pthread_mutex_lock(args->mxQuitflag); 
            pthread_cancel(*args->scanf_tid); // cancel scanf thread
            char* input = malloc(sizeof(char)*MAX_COMMAND_SIZE); 
            printf("Are you sure you want to exit? [y/n]:");
            scanf("%s", input);
            if(strcmp(input, "y") == 0)
            {
                *args->Quitflag = 1;
                pthread_mutex_unlock(args->mxQuitflag);
                free(input);
                return NULL;
            }
            else
            {
                free(input);
            }
            pthread_mutex_unlock(args->mxQuitflag);    
        }
    }
    free(args);
    return NULL;
}

void* time_thread_work(void *voidPtr)
{
    time_thread_args *args = voidPtr;
    sleep(args->t - LAST_REMIDER_TIME); 
    // Reminder
    pthread_mutex_lock(args->mxinterrupt);
    *args->interrupt = 1;
    pthread_mutex_unlock(args->mxinterrupt);    
    pthread_cancel(*args->scanf_tid); // terminate scanf    
    printf("\nLeft %ds\n",LAST_REMIDER_TIME);

    // sleep rest of time
    sleep(LAST_REMIDER_TIME); 
    pthread_mutex_lock(args->mx_is_time_up);
    *args->is_time_up = 1;
    pthread_mutex_unlock(args->mx_is_time_up);   
    pthread_mutex_lock(args->mxinterrupt);
    *args->interrupt = 1;
    pthread_mutex_unlock(args->mxinterrupt);    
    pthread_cancel(*args->scanf_tid); // terminate scanf
    return NULL;
}

void* d_thread_work(void* voidPtr)
{
    d_thread_args *args = voidPtr;
    printf("looking for .quiz files in DIR %s\n", args->d_path);
    if(chdir(args->d_path)) ERR("chdir"); 

    DIR *dirp;
    struct dirent *dp;
    struct stat filestat;

    if(NULL == (dirp = opendir(args->d_path))) ERR("opendir");

    char *word = malloc(sizeof(char)*MAX_COMMAND_SIZE);
    char *file_content;
    char *token;
    char *rest;
    do
    {
        // next one in dir
        if((dp = readdir(dirp)) != NULL) 
        {
            if(lstat(dp->d_name, &filestat)) ERR("lstat");
            else if(S_ISREG(filestat.st_mode)) // regular files
            {
                if(is_extension_valid(dp->d_name))
                {
                    printf("reading from file %s\n", dp->d_name);
                    get_file_content(dp->d_name,&file_content);
                    rest = file_content;            
                    while((token = strtok_r(rest, " \n", &rest)) != NULL)
                    {
                        strcpy(word,token);
                        //printf("%s Eng: %s \n",dp->d_name, token);
                        token = strtok_r(rest," \n", &rest);
                        //printf("%s PL: %s \n", dp->d_name, token);
                        if(is_question_new(args->path,word))
                        {
                            add_question(args->path,word,token);
                        }

                        // check quit after each question
                        pthread_mutex_lock(args->mxQuitflag);
                        if(*args->Quitflag == 1)
                        {
                            pthread_mutex_unlock(args->mxQuitflag);
                            free(file_content);
                            free(token);
                            free(word);
                            return NULL;
                        }
                        pthread_mutex_unlock(args->mxQuitflag);
                    }        
                    //free(file_content);
                }
            }
        }
    } while(dp != NULL); // read all in dir
    free(file_content);
    free(token);
    free(word);
    return NULL;
}


void* scanf_thread_work(void* voidPtr)
{
    scanf_thread_args* args = voidPtr;
    scanf("%s",args->input);
    return NULL;
}

void get_file_content(char *path, char **file_content)
{
    // size of file to malloc
    struct stat st;
    stat(path, &st);
    int size = st.st_size;

    // readfile 
    int in;
    if((in = open(path,O_RDONLY)) < 0) ERR("open");
    *file_content = malloc(sizeof(char)*size);
    if(read(in,*file_content,size) < 0) ERR("read");
    if(close(in)) ERR("close");
}

int count_lines(char* file_content)
{
    int counter = 0;
    for(int i = 0; i < strlen(file_content); i++)
    {
        if(file_content[i] == '\n') counter++;
    }
    return counter;
}

void add_question(char* path, char *word, char* translation)
{
    // create question line
    char *buf = malloc(sizeof(char)*(2*MAX_COMMAND_SIZE + 1)); 
    strcpy(buf,word);
    strcat(buf," ");
    strcat(buf,translation);
    strcat(buf,"\n");

    // adding to file
    int out;
    if((out = open(path,O_WRONLY|O_APPEND,0777)) < 0) ERR("open"); // - L385: dlaczego RDWR?
    if(write(out,buf,strlen(buf)) <= 0) ERR("write");
    if(close(out) < 0) ERR("close");
    free(buf);
}

int is_question_new(char* path, char *word)
{
    char *content;
    get_file_content(path,&content);
    
    char *token;
    char *rest = content;

    while((token = strtok_r(rest, " \n", &rest)))
    {
        if(strcmp(token,word) == 0)
        {
            return 0;
        }
    }
    return 1;
}

void insert_in_quiztab(char* file_content, int lines, char ***quiz_tab)
{
    char *token;
    char *rest = file_content;

    for(int i = 0; i < lines;i++)
    {
        token = strtok_r(rest, " \n", &rest);
        strcpy(quiz_tab[i][0], token);
        token = strtok_r(rest, " \n", &rest);
        strcpy(quiz_tab[i][1], token);
    }
}

void shuffle_questions(int *numbers, int questions_in_file)
{
    // create array with numbers of questions
    for(int i = 0; i < questions_in_file; i++)
    {
        numbers[i] = i;
    }
     
    // shuffle array with numbers of questions
    srand(time(NULL));
    int k; // random index
    int val; // value for elements swap
    for(int j = 0; j < questions_in_file; j++)
    {
        k = rand()%questions_in_file;
        val = numbers[j];
        numbers[j] = numbers[k];
        numbers[k] = val;            
    }
}

void test_user(char ***quiz_tab, int questions_in_file, int* correct, int n, int *is_time_up, pthread_mutex_t* mx_is_time_up, int *Quitflag, pthread_mutex_t* mx_quitflag, pthread_t* scanf_tid, int *interrupt, pthread_mutex_t* mx_interupt)
{
    // create array with numbers of questions
    int *numbers = malloc(sizeof(int)*questions_in_file);
    shuffle_questions(numbers, questions_in_file);

    char* input = malloc(sizeof(char) * MAX_COMMAND_SIZE);
    scanf_thread_args s_args;
    s_args.input = input;
    s_args.tid = scanf_tid;    

    for(int i = 0; i < n; i++)
    {
        // check time is up flag of time thread
        pthread_mutex_lock(mx_is_time_up);
        if(*is_time_up == 1)
        {
            printf("\nTime is up!\n");
            pthread_mutex_unlock(mx_is_time_up);
            break;
        }
        pthread_mutex_unlock(mx_is_time_up);

        // ask next question
        printf("%d. %s: ",i+1,quiz_tab[numbers[i%questions_in_file]][0]);
       
        // separated thread for scanf
        pthread_create(s_args.tid, NULL, scanf_thread_work,&s_args);
        pthread_join(*s_args.tid, NULL);   

        // check exitflag of quit thread
        pthread_mutex_lock(mx_quitflag);
        if(*Quitflag == 1)
        {
            pthread_mutex_unlock(mx_quitflag);
            break;
        }
        pthread_mutex_unlock(mx_quitflag);

        pthread_mutex_lock(mx_interupt);
        if(*interrupt == 0) // input was not interrupted -> check user input
        {
            pthread_mutex_unlock(mx_interupt);
            if(strcmp(input,quiz_tab[numbers[i%questions_in_file]][1]) == 0) 
            {
                printf("answer is correct\n");
                (*correct)++;
            }
            else printf("bad answer\n"); 
        } 
        else // input was interrupted -> ask this question again
        {
            pthread_mutex_unlock(mx_interupt);
            strcpy(input,"");
            i--; // we will ask this question again
        }

        //reset interruption status
        pthread_mutex_lock(mx_interupt); 
        *interrupt = 0;
        pthread_mutex_unlock(mx_interupt);

    }    
    free(input);
    free(numbers);
}

void print_stats(int correct, int n)
{
    printf("Final score: %d/%d", correct,n);
    int score =  correct*100/n;
    if(score >= 90) printf(" grade S\n");
    else if(score >= 80) printf(" grade A\n");
    else if(score >= 70) printf(" grade B\n");
    else if(score >= 60) printf(" grade C\n");
    else if(score >= 50) printf(" grade D\n");
    else printf(" grade E\n");
}

void quiz_mode(int argc, char **argv)
{
    // read arguments of quiz mode
    int n;
    int t;
    char *path;
    read_arg_quizmode(argc,argv,&n,&t,&path);
    
    // welcome with $USER environment variable
    char *name = getenv("USER");
    if(name) printf("Welcome %s in quiz mode!\n",name);
    else ERR("getenv");
    printf("You have %d s for this test\n", t);

    // flags for time and quit thread
    int is_time_up = 0; 
    int Quitflag = 0;
    int interrupt = 0;
    pthread_t scanf_tid; // tid to scanf thread to cancel it

    // quit thread preparation
    quit_thread_args q_args;
    pthread_mutex_t mx_quitflag = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_t mx_interruption = PTHREAD_MUTEX_INITIALIZER;
    q_args.mxQuitflag = &mx_quitflag;
    q_args.mxinterrupt = &mx_interruption;
    q_args.Quitflag = &Quitflag;
    q_args.interrupt = &interrupt;
    q_args.scanf_tid = &scanf_tid;
    
    // quit thread 
    sigset_t oldMask, newMask;
    sigemptyset(&newMask);
    sigaddset(&newMask, SIGINT);
    sigaddset(&newMask, SIGQUIT);
    if(pthread_sigmask(SIG_BLOCK, &newMask, &oldMask)) ERR("SIG_BLOCK error");
    q_args.pMask = &newMask;
    int q_err = pthread_create(&(q_args.tid), NULL, quit_thread_work, &q_args);
    if(q_err != 0) ERR("Couldn't create quit thread");

    // time thread
    time_thread_args t_args;
    t_args.t = t;
    t_args.is_time_up = &is_time_up;
    pthread_mutex_t mx_is_time_up = PTHREAD_MUTEX_INITIALIZER;
    t_args.mx_is_time_up = &mx_is_time_up;
    t_args.scanf_tid = &scanf_tid;
    t_args.mxinterrupt = &mx_interruption;
    t_args.interrupt = &interrupt;
    int t_err = pthread_create(&(t_args.tid), NULL, time_thread_work, &t_args);
    if(t_err != 0) ERR("Couldn't create time thread");
    
    // file in one string
    char *file_content;
    get_file_content(path,&file_content);

    // count how many lines are in that string
    int questions_in_file = count_lines(file_content);

    // allocate memory
    char ***quiz_tab;
    quiz_tab = malloc(sizeof(char**)*questions_in_file);
    for(int i = 0; i < questions_in_file; i++)
    {
        quiz_tab[i] = malloc(sizeof(char*)*2);
        quiz_tab[i][0] = malloc(sizeof(char)*MAX_COMMAND_SIZE);
        quiz_tab[i][1] = malloc(sizeof(char)*MAX_COMMAND_SIZE);
    }
    
    // read data from file to our table
    insert_in_quiztab(file_content,questions_in_file,quiz_tab);

    // run test for user
    int correct = 0; // correct answers for statistics
    test_user(quiz_tab, questions_in_file,&correct,n,&is_time_up, &mx_is_time_up, &Quitflag, &mx_quitflag, &scanf_tid, &interrupt,&mx_interruption);

    // free allocated memory
    for(int i = 0; i < questions_in_file; i++)
    {
        free(quiz_tab[i][0]);
        free(quiz_tab[i][1]);        
        free(quiz_tab[i]);
    }
    free(quiz_tab);

    // quit safety with statistics and cancel quit_thread if time is up or user finished test in time
    pthread_mutex_lock(&mx_quitflag);
    if(Quitflag != 1)
    {
        pthread_mutex_unlock(&mx_quitflag);
        print_stats(correct,n);
        pthread_cancel(q_args.tid);
        // - L628 wątek anulowany tez musi byc joinowany.
        pthread_join(q_args.tid,NULL);
    }   
    else
    {
        pthread_mutex_unlock(&mx_quitflag);
        pthread_join(q_args.tid,NULL);
    }

    // cancel time thread if user finished before time
    pthread_mutex_lock(&mx_is_time_up);
    if(is_time_up == 0)
    {
        pthread_mutex_unlock(&mx_is_time_up);
        pthread_cancel(t_args.tid);
    }
    else
    {
        pthread_mutex_unlock(&mx_is_time_up);
        pthread_join(t_args.tid,NULL);
    }
}

void create_quiz_mode(int argc, char **argv)
{
    // read arguments
    char *path;
    char *dir_path;
    read_arg_createmode(argc,argv,&path,&dir_path);
    char *name = getenv("USER");
    if(name) printf("Welcome %s in create quiz mode!\n",name);
    else ERR("getenv");

    // create_file
    int in; 
    if((in = open(path,O_CREAT|O_RDWR|O_APPEND,0777)) < 0) ERR("open");

    // scanf_thread
    pthread_t scanf_tid; // tid to scanf thread to cancel it
    char* input = malloc(sizeof(char) * MAX_COMMAND_SIZE);
    scanf_thread_args s_args;
    s_args.input = input;
    s_args.tid = &scanf_tid; 

    // quit thread
    int Quitflag = 0;
    int interrupt = 0;
    pthread_mutex_t mx_quitflag = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_t mx_interrupt = PTHREAD_MUTEX_INITIALIZER;
    quit_thread_args q_args;
    q_args.Quitflag = &Quitflag;
    q_args.interrupt = &interrupt;
    q_args.mxQuitflag = &mx_quitflag;
    q_args.mxinterrupt = &mx_interrupt;
    q_args.scanf_tid = &scanf_tid;
    sigset_t oldMask, newMask;
    sigemptyset(&newMask);
    sigaddset(&newMask, SIGINT);
    sigaddset(&newMask, SIGQUIT);
    if(pthread_sigmask(SIG_BLOCK, &newMask, &oldMask)) ERR("SIG_BLOCK error");
    q_args.pMask = &newMask;
    int q_err = pthread_create(&(q_args.tid), NULL, quit_thread_work, &q_args);
    if(q_err != 0) ERR("Couldn't create quit thread");

    // run d_thread which will take data from .quiz files in d_path
    if(strcmp(dir_path,"")!=0)
    {
        d_thread_args d_args;
        d_args.d_path = malloc(sizeof(char)*MAX_COMMAND_SIZE);
        d_args.path = malloc(sizeof(char)*MAX_COMMAND_SIZE);
        strcpy(d_args.d_path, dir_path);
        strcpy(d_args.path, path);
        d_args.mxQuitflag = &mx_quitflag;
        d_args.Quitflag = &Quitflag;
        int dt_err = pthread_create(&(d_args.tid), NULL, d_thread_work,&d_args);
        if(dt_err != 0) ERR("Couldn't create time thread");
        pthread_join(d_args.tid,NULL);
    }

    // add new word and translation to quiz file
    char english_word[MAX_COMMAND_SIZE];
    char translation[MAX_COMMAND_SIZE]; 
    while(1)
    {
        strcpy(english_word,"");
        strcpy(translation,"");
        printf("[word translation] (type exit to exit)\n");


        pthread_mutex_lock(&mx_quitflag);
        if(Quitflag == 1)
        {
            pthread_mutex_unlock(&mx_quitflag);
            break;
        }
        else
        {
            pthread_mutex_unlock(&mx_quitflag);
            // 1st scanf
            pthread_create(s_args.tid, NULL, scanf_thread_work, &s_args);
            pthread_join(*s_args.tid,NULL);
            strcpy(english_word,input);
            if(strcmp(english_word, "exit") == 0) break;
        }

        pthread_mutex_lock(&mx_quitflag);
        if(Quitflag == 1)
        {
            pthread_mutex_unlock(&mx_quitflag);
            break;
        }
        else
        {
            pthread_mutex_unlock(&mx_quitflag);
            pthread_mutex_lock(&mx_interrupt);
            if(interrupt == 0)
            {
                pthread_mutex_unlock(&mx_interrupt);
                // 2st scanf
                pthread_create(s_args.tid, NULL, scanf_thread_work, &s_args);
                pthread_join(*s_args.tid,NULL);
                strcpy(translation,input);
                if(strcmp(translation, "exit") == 0) break;
            }
            else
                pthread_mutex_unlock(&mx_interrupt);
            
        }        
        // add to file
        pthread_mutex_lock(&mx_quitflag);
        if(Quitflag == 1)
        {
            pthread_mutex_unlock(&mx_quitflag);
            break;
        }
        else
        {
            pthread_mutex_unlock(&mx_quitflag);
            pthread_mutex_lock(&mx_interrupt);
            if(interrupt == 0)
            {
                pthread_mutex_unlock(&mx_interrupt);
                if(strcmp(english_word,"") != 0 && strcmp(translation,"") != 0) //  - L728: miało byc && czy ||?
                {
                    if(is_question_new(path,english_word))
                    {
                        add_question(path,english_word,translation);
                        printf("[%s %s] was added\n", english_word, translation);
                    }
                    else
                        printf("Translation of %s already exists in %s\n", english_word, path);  
                }
            }
            else
                pthread_mutex_unlock(&mx_interrupt);
            
        }


        pthread_mutex_lock(&mx_interrupt);
        interrupt = 0;
        pthread_mutex_unlock(&mx_interrupt);

    }
    free(path);
    free(dir_path);
}

int main(int argc, char **argv)
{
    if(argc == 1) usage(argv[0]);  
    if(strcmp(argv[1],"-q") == 0) quiz_mode(argc,argv);
    else if(strcmp(argv[1],"-c") == 0) create_quiz_mode(argc,argv);
    else usage(argv[0]);
    exit(EXIT_SUCCESS);
}