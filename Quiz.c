#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__,__LINE__),exit(EXIT_FAILURE))
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <stdio.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#define QUESTIONS 10
#define MAX_PATH 300
#define DEFAULT_TIME 120
#define MAX_COMMAND_SIZE 100
#define QUIZFILE_EXT ".quiz"

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

int is_valid_extensions(char *path)
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
    if(!is_valid_extensions(*path))
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

    if(!is_valid_extensions(*path))
    {
        printf("File should have %s extension\n", QUIZFILE_EXT);
        usage(argv[0]);
    }
}

void quiz_mode(int argc, char **argv)
{
    // read arguments of quiz mode
    int questions;
    int time;
    char *path;
    read_arg_quizmode(argc,argv,&questions,&time,&path);

    // welcome with $USER environment variable
    char *name = getenv("USER");
    if(name) printf("Welcome %s in quiz mode!\n",name);
    else ERR("getenv");

    
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
    if((out = open(path,O_CREAT|O_RDWR|O_APPEND,0777)) < 0) ERR("open");
    if(write(out,buf,strlen(buf)) <= 0) ERR("write");
    if(close(out)) ERR("close");
}

int is_question_new(char *path, char *word)
{
    char *line = malloc(sizeof(char)*MAX_COMMAND_SIZE);
    size_t len = 0;
    FILE* stream = fopen(path,"r");
    if(!stream) return 1; // if file doesnt exist

    while(getline(&line,&len, stream) != -1)
    {
        if(strncmp(line,word,strlen(word)) == 0) // question already in this quiz file
        {
            fclose(stream);
            return 0; 
        }
    }
    return 1;
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

    // add new word and translation to quiz file
    char english_word[MAX_COMMAND_SIZE];
    char translation[MAX_COMMAND_SIZE];  
    printf("[word translation] (type exit to exit)\n");
    while(1) // exit after "exit" input
    {
        if(fscanf(stdin, "%s", english_word) < 0) ERR("fscanf");  
        if(strcmp(english_word, "exit") == 0) break;
        if(fscanf(stdin, "%s", translation) < 0) ERR("fscanf");  
        if(strcmp(translation, "exit") == 0) break;
        //printf("[%s %s]\n", english_word, translation);

        // if a question is new we will add it to file
        if(is_question_new(path,english_word))
        {
            add_question(path,english_word,translation);
            printf("[%s %s] was added\n", english_word, translation);
        }
        else
            printf("Translation of %s already exists in %s\n", english_word, path);    
    }


}

int main(int argc, char **argv)
{
    if(argc == 1) usage(argv[0]);  
    if(strcmp(argv[1],"-q") == 0) quiz_mode(argc,argv);
    else if(strcmp(argv[1],"-c") == 0) create_quiz_mode(argc,argv);
    else usage(argv[0]);
}
