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
    sigset_t *pMask;
} quit_thread_args;

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

int is_extension_valid(char *path)
{
    char *dot = strrchr(path,'.');
    if(dot == NULL) return 0;
    if(strcmp(dot,QUIZFILE_EXT) != 0) return 0;
    return 1;
}

void read_arg_quizmode(int argc, char **argv, int *questions, int* time, char **path)
{
    *questions = QUESTIONS;
    *time = DEFAULT_TIME;
    *path = malloc(sizeof(char)*MAX_PATH); 
    int is_p_set = 0;

    char c;
    optind = 2; // we ignore first one which is work_mode
    while ((c = getopt(argc, argv, "p:n:t:")) != -1)
    {
        switch (c)
        {
            case 'p':
                strcpy(*path,optarg);
                //if(optopt != 'p') 
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

int count_lines(char*path)
{
    int in;
    if((in = open(path,O_RDONLY)) < 0) ERR("open");
    char c;
    int questions_in_file = 0;
    while(read(in,&c,1) > 0)
    {
        if(c == '\n')
        {
            questions_in_file++;
        }
    }
    return questions_in_file;
}

void read_data(char* path, char ***quiz_tab)
{
    int in;
    if((in = open(path,O_RDONLY)) < 0) ERR("open");

    int actual_word_flag = 0; // 0 -> first word 1 -> second word
    int curr = 0;
    int ind = 0;
    char *first_word = malloc(sizeof(char)*MAX_COMMAND_SIZE);
    char *second_word = malloc(sizeof(char)*MAX_COMMAND_SIZE);
    char c;

    if(lseek(in,0,SEEK_SET) < 0) ERR("lseek");
    while(read(in,&c,1) > 0) // count how many lines;
    {
        if(c == ' ')
        {
            strcpy(quiz_tab[curr][0], first_word);
            strcpy(first_word, "");
            ind = 0;
            actual_word_flag = 1;
        }
        else if(c == '\n')
        {
            strcpy(quiz_tab[curr][1], second_word);
            strcpy(second_word, "");
            curr++;
            actual_word_flag = 0;
            ind = 0;
        }            
        else
        {
            if(actual_word_flag == 0)
            {
                first_word[ind] = c;
                first_word[ind+1] = '\0';
                ind++;
            }
            else
            {
                second_word[ind] = c;
                second_word[ind+1] = '\0';
                ind++;
            } 
        }
    }
    close(in);
    free(first_word);
    free(second_word);

}

void test_user(char ***quiz_tab, int questions_in_file, int* correct, int n, int *is_time_up, pthread_mutex_t* mx_is_time_up)
{
    // create array with numbers of questions
    int *numbers = malloc(sizeof(int)*questions_in_file);
    for(int i = 0; i < questions_in_file; i++)
    {
        numbers[i] = i;
    }
     
    // shuffle 
    srand(time(NULL));
    for(int j = 0; j < questions_in_file; j++)
    {
        int k = rand()%questions_in_file;
        int val = numbers[j];
        numbers[j] = numbers[k];
        numbers[k] = val;            
    }

    char* answer = malloc(sizeof(char) * MAX_COMMAND_SIZE);
    for(int i = 0; i < n; i++)
    {
        pthread_mutex_lock(mx_is_time_up);
        if(*is_time_up == 1)
        {
            printf("Time is up!\n");
            pthread_mutex_unlock(mx_is_time_up);
            break;
        }
        pthread_mutex_unlock(mx_is_time_up);

        printf("%d. %s: ",i+1,quiz_tab[numbers[i%questions_in_file]][0]);
        scanf("%s",answer);
        if(strcmp(answer,quiz_tab[numbers[i%questions_in_file]][1]) == 0) 
        {
            printf("answer is correct\n");
            (*correct)++;
        }
        else printf("bad answer\n");
    }    
    free(answer);
    free(numbers);
}

void* time_thread_work(void *voidPtr)
{
    time_thread_args *args = voidPtr;
    sleep(args->t - LAST_REMIDER_TIME);
    printf("\nLeft %ds\n",LAST_REMIDER_TIME);
    sleep(LAST_REMIDER_TIME);
    pthread_mutex_lock(args->mx_is_time_up);
    *args->is_time_up = 1;
    pthread_mutex_unlock(args->mx_is_time_up);    
    // send SIGUSR1 to terminate scanf
    kill(0,SIGUSR1);
    return NULL;
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

void sethandler(void (*f)(int), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;
    if(-1 == sigaction(sigNo, &act, NULL)) ERR("sigacion");
}

void sig_handler(int sig)
{
    if(sig == SIGUSR1)
        printf("\n"); // terminate scanf
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

    int correct = 0; // correct answers for statistics
    int is_time_up = 0; 

    time_thread_args args;
    args.t = t;
    args.is_time_up = &is_time_up;
    pthread_mutex_t mxquitflag = PTHREAD_MUTEX_INITIALIZER;
    args.mx_is_time_up = &mxquitflag;

    int err = pthread_create(&(args.tid), NULL, time_thread_work, &args);
    if(err != 0) ERR("Couldn't create time thread");

    sethandler(sig_handler, SIGUSR1);
    // count all lines
    int questions_in_file = count_lines(path);

    // prepare tab with quiz words
    char ***quiz_tab;
    quiz_tab = malloc(sizeof(char**)*questions_in_file);
    for(int i = 0; i < questions_in_file; i++)
    {
        quiz_tab[i] = malloc(sizeof(char*)*2);
        quiz_tab[i][0] = malloc(sizeof(char)*MAX_COMMAND_SIZE);
        quiz_tab[i][1] = malloc(sizeof(char)*MAX_COMMAND_SIZE);
    }
    
    // read data from file to our table
    read_data(path,quiz_tab);

    test_user(quiz_tab, questions_in_file,&correct,n,&is_time_up, &mxquitflag);

    print_stats(correct,n);     

    for(int i = 0; i < questions_in_file; i++)
    {
        free(quiz_tab[i][0]);
        free(quiz_tab[i][1]);        
        free(quiz_tab[i]);
    }
    free(quiz_tab);

    if(is_time_up == 0)
    {
        pthread_cancel(args.tid);
    }
    else
    {
        pthread_join(args.tid,NULL);
    }
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
    if((out = open(path,O_RDWR|O_APPEND,0777)) < 0) ERR("open");
    if(write(out,buf,strlen(buf)) <= 0) ERR("write");
    if(close(out)) ERR("close");
    free(buf);
}

int is_question_new(char *path, char *word)
{
    // open file only to read
    int in;
    if((in = open(path,O_RDONLY)) < 0) ERR("open");

    char *buf = malloc(sizeof(char)*MAX_COMMAND_SIZE);
    char c;
    int act_word_size = 0; 
    int ignore = 0; // we will check only first word in line
    while(read(in,&c,1) != 0) // exit if we reach end of file
    {
        if(c == ' ') // end of a first word in a line
        {
            if(act_word_size == strlen(word)) // words can be the same only if they have the same length
            {
                if(strncmp(buf,word,act_word_size) == 0)
                {
                    free(buf);
                    return 0; 
                }
            }
            ignore = 1; // ignore second part of a line
        }
        if(ignore == 0) // first word in a line
        {
            buf[act_word_size] = c;
            buf[act_word_size+1] = '\0';
            act_word_size++;
        }       
        if(c == '\n') // end of a line
        {
            act_word_size = 0;
            ignore = 0;
        } 
    }
    free(buf);
    return 1;
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

    char*word = malloc(sizeof(char)*MAX_COMMAND_SIZE);
    char*translation = malloc(sizeof(char)*MAX_COMMAND_SIZE);


    do
    {
        errno = 0;
        if((dp = readdir(dirp)) != NULL)
        {
            if(lstat(dp->d_name, &filestat)) ERR("lstat");
            else if(S_ISREG(filestat.st_mode))
            {
                if(is_extension_valid(dp->d_name))
                {
                    printf("reading from file: %s\n", dp->d_name);
                    int start_pos = 0;
                    int new_bytes = 0;
                    do
                    {
                        pthread_mutex_lock(args->mxQuitflag);
                        if(*args->Quitflag == 1)
                        {
                            pthread_mutex_unlock(args->mxQuitflag);
                            break;
                        }
                        pthread_mutex_unlock(args->mxQuitflag);

                        new_bytes = read_one_line(dp->d_name, word, translation, start_pos);
                        start_pos += new_bytes;
                        if(is_question_new(args->path,word))
                            add_question(args->path,word,translation);
                    } while (new_bytes != 0);     


                    pthread_mutex_lock(args->mxQuitflag);
                    if(*args->Quitflag == 1)
                    {
                        pthread_mutex_unlock(args->mxQuitflag);
                        break;
                    }
                    pthread_mutex_unlock(args->mxQuitflag);           
                }   
            }
        }
    } while(dp != NULL);
    free(word);
    free(translation);
    return NULL;
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
            pthread_mutex_lock(args->mxQuitflag);
            
            char* input = malloc(sizeof(char)*MAX_COMMAND_SIZE);
            kill(0,SIGUSR1);  
            //printf("\n");          
            printf("\nAre you sure you want to exit? [y/n]:");
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
                printf("continue then\n");
            }
            pthread_mutex_unlock(args->mxQuitflag);    
        }
        if(signo == SIGUSR1)
        {
            printf("\n");
        }
    }
    free(args);
    return NULL;
}

int read_one_line(char*path, char* word, char* translation, int start_pos)
{
    int in; 
    if((in = open(path,O_RDONLY)) < 0) ERR("open");

    char *first_word = malloc(sizeof(char)*MAX_COMMAND_SIZE);
    char *second_word = malloc(sizeof(char)*MAX_COMMAND_SIZE);

    int ind = 0;
    int actual_word_flag = 0;
    char c;
    int bytes_read = 0;

    if(lseek(in,start_pos,SEEK_SET) < 0) ERR("lseek");
    while(read(in,&c,1) > 0) 
    {
        bytes_read++;
        if(c == ' ')
        {
            strcpy(word,first_word);
            strcpy(first_word, "");
            ind = 0;
            actual_word_flag = 1;
        }
        else if(c == '\n')
        {
            strcpy(translation,second_word);
            strcpy(second_word, "");
            break;
        }            
        else
        {
            if(actual_word_flag == 0)
            {
                first_word[ind] = c;
                first_word[ind+1] = '\0';
                ind++;
            }
            else
            {
                second_word[ind] = c;
                second_word[ind+1] = '\0';
                ind++;
            } 
        }
    }
    close(in);
    free(first_word);
    free(second_word);
    return bytes_read;
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

    int Quitflag = 0;
    pthread_mutex_t mx_quitflag = PTHREAD_MUTEX_INITIALIZER;

    quit_thread_args q_args;
    q_args.Quitflag = &Quitflag;
    q_args.mxQuitflag = &mx_quitflag;
    

    sethandler(sig_handler,SIGUSR1);
    // signal mask
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
    printf("[word translation] (type exit to exit)\n");
    while(1) // exit after "exit" input
    {
        pthread_mutex_lock(&mx_quitflag);
        if(Quitflag == 1)
        {
            pthread_mutex_unlock(&mx_quitflag);
            break;
        }
        else
        {
            pthread_mutex_unlock(&mx_quitflag);
            scanf("%s", english_word);  
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
            scanf("%s", translation);  
            if(strcmp(translation, "exit") == 0) break;
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
            if(is_question_new(path,english_word))
            {
                add_question(path,english_word,translation);
                printf("[%s %s] was added\n", english_word, translation);
            }
            else
                printf("Translation of %s already exists in %s\n", english_word, path);    
   
        }
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
