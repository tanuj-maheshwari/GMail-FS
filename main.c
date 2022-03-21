#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <time.h>

#define FUSE_USE_VERSION 30

#include <fuse.h>
#include <curl/curl.h>

/* This struct is used by writeCallback */
struct memobj
{
    char *memory;
    size_t size, allocated;
};

/* This struct is used by write function */
struct upload_status
{
    size_t bytes_read;
    char message[4000];
};

/*Strcuture for File object*/
struct fileobj
{
    int uid;             /*UID within folder*/
    char disp_name[200]; /*Display name (by subject)*/
    //MODE (permission) to be added
};

/*Strcuture for Directory object*/
struct directoryobj
{
    char name[50]; /*Name of directory*/
    //char relative_path[100];   /*Reletive path*/
    int num_files;             /*Number of files in directory*/
    int next_uid;              /*Next uid for mail*/
    struct fileobj files[500]; /*Files in the directory*/
    bool in_memory;            /*If the files in directory are loaded from server*/
    //MODE (permission) to be added
};

/*Structure for Root Directory*/
struct root_directory
{
    int num_directories;                  /*Number of directories*/
    struct directoryobj directories[100]; /*Directories under root*/
};

struct root_directory *filesystem; /*Virtual Filesystem*/
char *last_read_mail;              /*Last mail read*/
bool is_cached;                    /*If caching in main memory is used*/

char *username;    /*Username (usually email address)*/
char *password;    /*Password*/
char *imap_server; /*IMAP server URL*/
char *smtp_server; /*SMTP server URL*/

/**
 * Write callback function used by curl
 * @param ptr
 * @param size
 * @param nmemb
 * @param userdata
 * @return Number of bytes written
 */
size_t writeCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    size_t realsize = size * nmemb;
    struct memobj *mem = (struct memobj *)userdata;

    if (!realsize)
    {
        return 0;
    }
    if ((mem->size + realsize + 1) > mem->allocated)
    {
        size_t bytes = 65536 + mem->size + realsize + 1;
        char *temp = (char *)realloc(mem->memory, bytes);
        if (!temp)
        {
            fprintf(stderr, "Error: Out of memory, realloc returned NULL.");
            return 0;
        }
        mem->memory = temp;
        mem->allocated = bytes;
    }

    memcpy(&(mem->memory[mem->size]), ptr, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

/**
 * Read callback function used by curl
 * @param ptr
 * @param size
 * @param nmemb
 * @param userp
 * @return Number of bytes read
 */
static size_t readCallback(char *ptr, size_t size, size_t nmemb, void *userp)
{
    struct upload_status *upload_ctx = (struct upload_status *)userp;
    const char *data;
    size_t room = size * nmemb;

    if ((size == 0) || (nmemb == 0) || ((size * nmemb) < 1))
    {
        return 0;
    }

    data = &upload_ctx->message[upload_ctx->bytes_read];

    if (*data)
    {
        size_t len = strlen(data);
        if (room < len)
            len = room;
        memcpy(ptr, data, len);
        upload_ctx->bytes_read += len;

        return len;
    }

    return 0;
}

/**
 * Gets IMAP URL for a given local directory
 * @param path Relative path to local directory
 * @return URL
 */
char *getIMAP_URL(const char *path)
{
    char *url = (char *)malloc(150 * sizeof(char));
    strcpy(url, "");
    //strcat(url, "imaps://imap.gmail.com:993/");
    strcat(url, imap_server);
    strcat(url, "/");

    /*For folders having [Gmail]*/
    if (strcmp(path + 1, "All") == 0 || strcmp(path + 1, "Drafts") == 0 || strcmp(path + 1, "Important") == 0 || strcmp(path + 1, "Sent") == 0 || strcmp(path + 1, "Spam") == 0 || strcmp(path + 1, "Starred") == 0 || strcmp(path + 1, "Trash") == 0)
    {
        strcat(url, "%5BGmail%5D/");
    }

    strcat(url, path + 1);

    /*For All Mail and Sent Mail*/
    if (strcmp(path + 1, "All") == 0 || strcmp(path + 1, "Sent") == 0)
    {
        strcat(url, "%20Mail");
    }
    strcat(url, "/");

    return url; //return the generated URL
}

/**
 * Gets data from IMAP server
 * @return Retrieved data
 */
char *getDataFromIMAPServer()
{
    CURL *curl;
    CURLcode res = CURLE_OK;
    struct memobj body = {
        0,
    };
    curl = curl_easy_init(); //initilaise curl
    //check for errors
    if (!curl)
    {
        printf("\n\nProblem with curl_easy_init()\n\n");
        exit(-1);
    }
    //set curl arguments
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&body);
    curl_easy_setopt(curl, CURLOPT_URL, imap_server);
    curl_easy_setopt(curl, CURLOPT_USERNAME, username);
    curl_easy_setopt(curl, CURLOPT_PASSWORD, password);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0);
    //execute curl command
    res = curl_easy_perform(curl);
    if (res != CURLE_OK)
    {
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
    }
    //curl cleanup
    curl_easy_cleanup(curl);

    return body.memory; //return the data from server
}

/**
 * Initialises directories in root
 */
void initialiseRootDirectories()
{
    /*Get data from server*/
    char *data = getDataFromIMAPServer();

    /*Keep obtaining folder names*/
    while (strstr(data, ")"))
    {
        data = strstr(data, ")");
        data += 7;
        /*If name folder name starts with [Gmail]*/
        if (*data == '[')
        {
            data += 8;
        }

        /*Obtain directory name till end or till space*/
        char dirname_from_server[50];
        strcpy(dirname_from_server, "");
        int i = 0;
        while (data[i] != '\"' && data[i] != ' ')
        {
            strncat(dirname_from_server, &data[i], 1);
            i++;
        }

        if (strstr(dirname_from_server, "*"))
        {
            continue;
        }

        /*Enter directory names*/
        strcpy(filesystem->directories[filesystem->num_directories].name, dirname_from_server);
        filesystem->directories[filesystem->num_directories].num_files = 0;
        filesystem->directories[filesystem->num_directories].next_uid = 1;
        filesystem->directories[filesystem->num_directories].in_memory = false;
        filesystem->num_directories++;
    }
}

/**
 * Initialise files for the given directory
 * @param path Path to directory
 */
void initialiseDirectory(const char *path)
{
    //get imap url for the directory
    char *url = getIMAP_URL(path);

    //get directory index within substyem
    int dir_index = -1;
    for (int i = 0; i < filesystem->num_directories; i++)
    {
        if (strcmp(filesystem->directories[i].name, path + 1) == 0)
        {
            dir_index = i;
            break;
        }
    }

    //get list of all uids in current folder (using custom request UID SEARCH ALL)
    CURL *curl;
    CURLcode res = CURLE_OK;
    struct memobj body = {
        0,
    };
    curl = curl_easy_init(); //initilaise curl
    //check for errors
    if (!curl)
    {
        printf("\n\nError in curl_easy_init()\n\n");
        exit(-1);
    }
    //set curl arguments
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&body);
    curl_easy_setopt(curl, CURLOPT_USERNAME, username);
    curl_easy_setopt(curl, CURLOPT_PASSWORD, password);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "UID SEARCH ALL");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0);
    //execute curl command
    res = curl_easy_perform(curl);
    if (res != CURLE_OK)
    {
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
    }
    //curl cleanup
    curl_easy_cleanup(curl);

    //run for all obtained uids
    char *data = strstr(body.memory, "SEARCH");
    data += 7;
    char num[10];
    strcpy(num, "");
    int i = 0;
    if (strlen(data) < 2)
    {
        free(url); //free memory
        return;
    }
    while (i <= strlen(data))
    {
        //if a uid is read
        if (data[i] == ' ' || data[i] == '\n')
        {
            //extract uid from num
            int uid = atoi(num);

            //generate url
            char *newurl = getIMAP_URL(path);
            strcat(newurl, ";UID=");
            if (data[i] == '\n')
            {
                num[strlen(num) - 1] = '\0';
            }
            strcat(newurl, num);
            strcpy(num, "");

            //curl commands to get the message
            CURL *newcurl;
            CURLcode newres = CURLE_OK;
            struct memobj newbody = {
                0,
            };
            struct memobj newhead = {
                0,
            };
            newcurl = curl_easy_init(); //initilaise curl
            //check for errors
            if (!newcurl)
            {
                printf("\n\nError in curl_easy_init()\n\n");
                exit(-1);
            }
            //set curl arguments
            curl_easy_setopt(newcurl, CURLOPT_WRITEFUNCTION, writeCallback);
            curl_easy_setopt(newcurl, CURLOPT_WRITEDATA, (void *)&newbody);
            curl_easy_setopt(newcurl, CURLOPT_USERNAME, username);
            curl_easy_setopt(newcurl, CURLOPT_PASSWORD, password);
            curl_easy_setopt(newcurl, CURLOPT_URL, newurl);
            curl_easy_setopt(newcurl, CURLOPT_SSL_VERIFYPEER, 0);
            curl_easy_setopt(newcurl, CURLOPT_SSL_VERIFYHOST, 0);
            //execute curl command
            newres = curl_easy_perform(newcurl);
            if (newres != CURLE_OK)
            {
                fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(newres));
            }
            //curl cleanup
            curl_easy_cleanup(newcurl);

            free(newurl); //free memory

            //extract subject from mail
            char *sub = strstr(newbody.memory, "Subject:");
            sub = sub + 9;
            char subject[100];
            int j = 0;
            while (sub[j] != '\n')
            {
                strncat(subject, &sub[j], 1);
                j++;
            }
            sscanf(sub, "%[^\n]s", subject);

            //make entry in directory object
            filesystem->directories[dir_index].files[filesystem->directories[dir_index].num_files].uid = uid;
            int a = 0;
            //replace space with _ in subject
            while (a < strlen(subject))
            {
                if (isspace(subject[a]))
                {
                    subject[a] = '_';
                }
                a++;
            }
            subject[strlen(subject) - 1] = '\0';
            strcat(subject, ".txt");
            strcpy(filesystem->directories[dir_index].files[filesystem->directories[dir_index].num_files].disp_name, subject);
            filesystem->directories[dir_index].num_files++;
            filesystem->directories[dir_index].next_uid = uid + 1 > filesystem->directories[dir_index].next_uid ? uid + 1 : filesystem->directories[dir_index].next_uid;

            //if end of uids
            if (data[i] == '\n')
            {
                break;
            }

            //if more uids available
            i++;
        }
        //if the uid spans for more digits
        else
        {
            strncat(num, &data[i], 1);
            i++;
        }
    }

    free(url); //free memory
}

/**
 * Checks if the given path is a directory
 * @param path Path to directory
 * @return If the path is a directory
 */
bool isDir(const char *path)
{
    //initialise root directories if not cached
    if (!is_cached)
    {
        filesystem->num_directories = 0;
        initialiseRootDirectories();
    }

    //check if the directory with given name is present or not
    for (int i = 0; i < filesystem->num_directories; i++)
    {
        if (strcmp(filesystem->directories[i].name, path + 1) == 0)
        {
            return true; //directory present
        }
    }
    return false; //directory absent
}

/**
 * Checks if the given path is a file
 * @param path Path to file
 * @return If the path is a file
 */
bool isFile(const char *path)
{
    //parse directory name
    char dirname[50];
    strcpy(dirname, "");
    sscanf(path + 1, "%[^/]s", dirname);

    //initialise root directories if not cached
    if (!is_cached)
    {
        filesystem->num_directories = 0;
        initialiseRootDirectories();
    }

    //loop for all directories
    for (int i = 0; i < filesystem->num_directories; i++)
    {
        //if found
        if (strcmp(filesystem->directories[i].name, dirname) == 0)
        {
            //initiliase the directory if not in memory or not cached
            if (!is_cached || !filesystem->directories[i].in_memory)
            {
                char dirpath[50];
                strcpy(dirpath, "/");
                strcat(dirpath, dirname);
                filesystem->directories[i].num_files = 0;
                filesystem->directories[i].next_uid = 1;
                initialiseDirectory(dirpath);
                filesystem->directories[i].in_memory = true;
            }

            //check if file is present in directory
            char *filename = strstr(path + 1, "/");
            filename += 1;
            for (int j = 0; j < filesystem->directories[i].num_files; j++)
            {
                if (strcmp(filesystem->directories[i].files[j].disp_name, filename) == 0)
                {
                    return true; //file present
                }
            }
        }
    }
    return false; //can't find file
}

/**
 * Function that gets the attributes of an object.
 * @param path Path to the object (file/folder)
 * @param st Object stats (permissions, type, etc)
 * @return Success/Failure
 */
static int do_getattr(const char *path, struct stat *st)
{
    //debug statement
    printf("\n[getattr called]\n\t %s path\n", path);

    //set file attributes
    st->st_uid = getuid();     // The owner of the file/directory
    st->st_gid = getgid();     // The group of the file/directory
    st->st_atime = time(NULL); // The last access of the file/directory
    st->st_mtime = time(NULL); // The last modification of the file/directory

    /*For directories*/
    if (strcmp(path, "/") == 0 || isDir(path))
    {
        st->st_mode = S_IFDIR | 0755;
        st->st_nlink = 2;
    }

    /*For files*/
    else if (isFile(path))
    {
        st->st_mode = S_IFREG | 0644;
        st->st_nlink = 1;
        st->st_size = 4000;
    }

    /*No match*/
    else
    {
        return -ENOENT; //error code
    }

    return 0; //success
}

/**
 * Function that gets called when listing contents of a folder.
 * @param path Path to the folder
 * @param buffer Buffer where the content read is written
 * @param filler Pointer to function to fill the buffer
 * @param offset Offset for writing to buffer
 * @param fi
 * @return Success/Failure
 */
static int do_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
    //debug statement
    printf("\n[readdir called]\n\t %s path\n", path);

    //add current and parent directory to directory list
    filler(buffer, ".", NULL, 0);  // Current Directory
    filler(buffer, "..", NULL, 0); // Parent Directory

    /*For root directory*/
    if (strcmp(path, "/") == 0)
    {
        //get root directories if not cached
        if (!is_cached)
        {
            filesystem->num_directories = 0;
            initialiseRootDirectories();
        }

        //list all folders in the root directory
        for (int i = 0; i < filesystem->num_directories; i++)
        {
            filler(buffer, filesystem->directories[i].name, NULL, 0);
        }
    }

    /*For level 1 directories*/
    else if (isDir(path))
    {
        //loop for all directories
        for (int i = 0; i < filesystem->num_directories; i++)
        {
            //if found
            if (strcmp(filesystem->directories[i].name, path + 1) == 0)
            {
                //initialise directory if not in memory or not cached
                if (!is_cached || !filesystem->directories[i].in_memory)
                {
                    //LOAD FILES IN MEMORY FOR THE GIVEN DIRECTORY
                    filesystem->directories[i].num_files = 0;
                    filesystem->directories[i].next_uid = 1;
                    initialiseDirectory(path);
                    filesystem->directories[i].in_memory = true;
                }

                //list all the files inside the directory
                for (int j = 0; j < filesystem->directories[i].num_files; j++)
                {
                    filler(buffer, filesystem->directories[i].files[j].disp_name, NULL, 0);
                }
            }
        }
    }
    else
    {
        return -ENOENT; //return error code
    }

    return 0; //success
}

/**
 * Function that gets called when reading from the file.
 * @param path Path to the file
 * @param buffer Buffer where the content read is written
 * @param size Size to be read
 * @param offset Offset for writing to buffer
 * @param fi
 * @return Number of bytes read
 */
static int do_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi)
{
    //debug message
    printf("\n[read called]\n\t %s path\n", path);

    char *content;      //content of file
    bool found = false; //if file is found

    /*Parse directory name*/
    char dirname[50];
    strcpy(dirname, "");
    sscanf(path + 1, "%[^/]s", dirname);

    //initialise directories if not cached
    if (!is_cached)
    {
        filesystem->num_directories = 0;
        initialiseRootDirectories();
    }
    //search for directory
    for (int i = 0; i < filesystem->num_directories; i++)
    {
        if (strcmp(filesystem->directories[i].name, dirname) == 0)
        {
            //initialise directory if not filled or not cached
            if (!is_cached || !filesystem->directories[i].in_memory)
            {
                char dirpath[50];
                strcpy(dirpath, "/");
                strcat(dirpath, dirname);
                filesystem->directories[i].num_files = 0;
                filesystem->directories[i].next_uid = 1;
                initialiseDirectory(dirpath);
                filesystem->directories[i].in_memory = true;
            }

            /*Search for file in directory*/
            char *filename = strstr(path + 1, "/");
            filename += 1;
            for (int j = 0; j < filesystem->directories[i].num_files; j++)
            {
                //if found
                if (strcmp(filename, filesystem->directories[i].files[j].disp_name) == 0)
                {
                    //generate url
                    char dirpath[100];
                    strcpy(dirpath, "/");
                    strcat(dirpath, dirname);
                    char *newurl = getIMAP_URL(dirpath);
                    strcat(newurl, ";UID=");
                    char num[10];
                    strcpy(num, "");
                    sprintf(num, "%d", filesystem->directories[i].files[j].uid);
                    strcat(newurl, num);

                    //curl commands to get the message
                    CURL *newcurl;
                    CURLcode newres = CURLE_OK;
                    struct memobj newbody = {
                        0,
                    };
                    newcurl = curl_easy_init(); //initialise curl
                    //check for errors
                    if (!newcurl)
                    {
                        printf("\n\nError in curl_easy_init()\n\n");
                        exit(-1);
                    }
                    //set curl attributes
                    curl_easy_setopt(newcurl, CURLOPT_WRITEFUNCTION, writeCallback);
                    curl_easy_setopt(newcurl, CURLOPT_WRITEDATA, (void *)&newbody);
                    curl_easy_setopt(newcurl, CURLOPT_USERNAME, username);
                    curl_easy_setopt(newcurl, CURLOPT_PASSWORD, password);
                    curl_easy_setopt(newcurl, CURLOPT_URL, newurl);
                    curl_easy_setopt(newcurl, CURLOPT_SSL_VERIFYPEER, 0);
                    curl_easy_setopt(newcurl, CURLOPT_SSL_VERIFYHOST, 0);
                    //execute curl command
                    newres = curl_easy_perform(newcurl);
                    if (newres != CURLE_OK)
                    {
                        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(newres));
                    }
                    //cleanup curl
                    curl_easy_cleanup(newcurl);

                    free(newurl); //free memory

                    //parse the content recieved to get only text part
                    char message[4000];
                    char *start = strstr(newbody.memory, "Content-Type: text/plain; charset=\"UTF-8\"");
                    //for messages received through GMail Compose
                    if (start)
                    {
                        start += 45;
                        strcpy(message, "");
                        char *end = strstr(start, "Content-Type: text/html; charset=\"UTF-8\"");

                        //if can't find end
                        if (!end)
                        {
                            return -EIO; //error code
                        }
                        int num_bytes_read = (int)(end - start) - 33;

                        //if mail too large
                        if (num_bytes_read > 4000)
                        {
                            return -EIO; //error code
                        }

                        //copy to message
                        strncpy(message, start, num_bytes_read);
                        message[num_bytes_read] = '\0';
                    }

                    //for messages received through SMTP
                    else
                    {
                        start = strstr(newbody.memory, "\r\n\r\n");

                        //if can't find start
                        if (!start)
                        {
                            return -EIO; //error code
                        }

                        //if mail read is too large
                        else if (strlen(start) > 4000)
                        {
                            return -EIO; //error code
                        }

                        start += 4;

                        //copy to message
                        strcpy(message, start);
                        message[strlen(start)] = '\0';
                    }

                    //read data into content and indicate that file is found
                    content = message;
                    found = true;
                }
            }
        }
    }

    //if file not found
    if (!found)
    {
        return -1;
    }

    //enter data into buffer
    strcpy(buffer, content + offset);

    //update last_read_mail
    strcpy(last_read_mail, buffer);

    return strlen(content) - offset; //return number of bytes read
}

/**
 * Function that gets called when a directory is created.
 * Directories can be crreated only under root directory.
 * Creating a directory is equvalent to making a new label.
 * @param path Path to the directory
 * @param mode Permission modes
 * @return Success/error code
 */
static int do_mkdir(const char *path, mode_t mode)
{
    //debug statement
    printf("\n[mkdir called]\n\t %s path\n", path);

    /*Get label_name from path*/
    path++;
    char *label_name = (char *)malloc((strlen(path) + 2) * sizeof(char));
    strcpy(label_name, "");
    for (int i = 0; i < strlen(path); i++)
    {
        label_name[i] = path[i];
    }
    label_name[strlen(path)] = '\0';

    /*Min label name length = 2*/
    if (strlen(label_name) < 2)
    {
        free(label_name); //free memory
        return -EINVAL;   //error code
    }

    /*get parent path*/
    path--;
    char parent_path[100];
    strcpy(parent_path, path);
    for (int i = strlen(path) - 1; i >= 0; i--)
    {
        if (parent_path[i] == '/')
        {
            break;
        }
        parent_path[i] = '\0';
    }
    if (strlen(parent_path) > 1)
    {
        parent_path[strlen(parent_path) - 1] = '\0';
    }

    /*if creating in root directory*/
    if (strcmp(parent_path, "/") == 0)
    {
        //get root directories if not cahcked
        if (!is_cached)
        {
            filesystem->num_directories = 0;
            initialiseRootDirectories();
        }

        /*Check if label already exists*/
        for (int i = 0; i < filesystem->num_directories; i++)
        {
            if (strcmp(label_name, filesystem->directories[i].name) == 0)
            {
                free(label_name); //free memory
                return -EEXIST;   //error code
            }
        }

        /*Create the directory in subsystem*/
        strcpy(filesystem->directories[filesystem->num_directories].name, label_name);
        filesystem->directories[filesystem->num_directories].in_memory = false;
        filesystem->directories[filesystem->num_directories].num_files = 0;
        filesystem->directories[filesystem->num_directories].next_uid = 1;
        filesystem->num_directories++;

        /*Send curl request to create label*/
        //generate custom request
        char create_req[60];
        strcpy(create_req, "CREATE ");
        strcat(create_req, label_name);
        CURL *curl;
        CURLcode res = CURLE_OK;
        struct memobj body = {
            0,
        };
        curl = curl_easy_init(); //initialise curl
        //check for errors
        if (!curl)
        {
            printf("\n\nError in curl_easy_init()\n\n");
            exit(-1);
        }
        //set curl arguments
        curl_easy_setopt(curl, CURLOPT_USERNAME, username);
        curl_easy_setopt(curl, CURLOPT_PASSWORD, password);
        curl_easy_setopt(curl, CURLOPT_URL, imap_server);
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, create_req);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0);
        //perform curl command
        res = curl_easy_perform(curl);
        if (res != CURLE_OK)
        {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        }
        //cleanup curl
        curl_easy_cleanup(curl);

        free(label_name); //free memory
        return 0;         //successful
    }
    /*If trying to create a directory in any directory other than root*/
    else if (isDir(parent_path))
    {
        free(label_name); //free memory
        return -EPERM;    //return error code
    }

    free(label_name); //free memory
    return -1;        //error
}

/**
 * Function that gets called when a file is deleted.
 * Only files with names not starting with digit can be deleted.
 * A file can't be deleted from the All folder.
 * @param path Path to the directory
 * @return Success/error code
 */
static int do_unlink(const char *path)
{
    //debug statement
    printf("\n[unlink called]\n\t %s path\n", path);

    //check if path refers to a file
    if (isFile(path))
    {
        //parse directory name from path
        char dirname[50];
        strcpy(dirname, "");
        sscanf(path + 1, "%[^/]s", dirname);

        //search for directory
        for (int i = 0; i < filesystem->num_directories; i++)
        {
            if (strcmp(filesystem->directories[i].name, dirname) == 0)
            {
                //get directory in memory (if not)
                if (!filesystem->directories[i].in_memory)
                {
                    char dirpath[50];
                    strcpy(dirpath, "/");
                    strcat(dirpath, dirname);
                    filesystem->directories[i].num_files = 0;
                    filesystem->directories[i].next_uid = 1;
                    initialiseDirectory(dirpath);
                    filesystem->directories[i].in_memory = true;
                }

                //serach for file within directory
                char *filename = strstr(path + 1, "/");
                filename += 1;
                for (int j = 0; j < filesystem->directories[i].num_files; j++)
                {
                    //file found
                    if (strcmp(filesystem->directories[i].files[j].disp_name, filename) == 0)
                    {
                        /*If filename begins with a digit*/
                        if (isdigit(filename[0]))
                        {
                            return -EPERM; //can't delete, return error code
                        }

                        //GET FILE UID, DELETE THE FILE
                        int uid = filesystem->directories[i].files[j].uid;
                        char dirpath[50];
                        strcpy(dirpath, "/");
                        strcat(dirpath, dirname);
                        char *url = getIMAP_URL(dirpath);

                        //delete mail from server
                        CURL *curl;
                        CURLcode res = CURLE_OK;
                        curl = curl_easy_init(); //initialise curl
                        //check for errors
                        if (!curl)
                        {
                            printf("\n\nError in curl_easy_init()\n\n");
                            exit(-1);
                        }
                        //make custom request
                        char custom_req[100];
                        strcpy(custom_req, "UID STORE ");
                        char num[10];
                        strcpy(num, "");
                        sprintf(num, "%d", uid);
                        strcat(custom_req, num);
                        strcat(custom_req, " FLAGS (\\Deleted)"); //add the /Deleted flag to the mail
                        //set curl arguments
                        curl_easy_setopt(curl, CURLOPT_USERNAME, username);
                        curl_easy_setopt(curl, CURLOPT_PASSWORD, password);
                        curl_easy_setopt(curl, CURLOPT_URL, url);
                        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, custom_req);
                        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
                        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0);
                        //perform curl command
                        res = curl_easy_perform(curl);
                        if (res != CURLE_OK)
                        {
                            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
                        }
                        //cleanup curl
                        curl_easy_cleanup(curl);

                        free(url); //free memory

                        //refresh the directory
                        filesystem->directories[i].in_memory = false;
                        filesystem->directories[i].num_files = 0;
                        filesystem->directories[i].next_uid = 1;

                        return 0; //success
                    }
                }
            }
        }
    }
    //if path is not a file
    else
    {
        return -ENOENT; //return error code
    }

    return -1; //error
}

/**
 * Function that gets called when a directory is deleted.
 * Only created labels can be deleted, not folders like INBOX, Drafts, etc.
 * Only those lables can be deleted whose 2nd character is an uppercase alphabet.
 * @param path Path to the directory
 * @return Success/error code
 */
static int do_rmdir(const char *path)
{
    //debug statement
    printf("\n[rmdir called]\n\t %s path\n", path);

    /*Only labels with 2nd character uppercase can be removed*/
    if (isDir(path) && isupper(path[2]) && strcmp(path + 1, "INBOX") != 0)
    {
        //search for all folders in root
        for (int i = 0; i < filesystem->num_directories; i++)
        {
            //if the label is found
            if (strcmp(filesystem->directories[i].name, path + 1) == 0)
            {
                //delete mailbox from server
                CURL *curl;
                CURLcode res = CURLE_OK;
                curl = curl_easy_init(); //initilise curl
                //check for error
                if (!curl)
                {
                    printf("\n\nError in curl_easy_init()\n\n");
                    exit(-1);
                }
                //create custom request for deleting mailbox
                char custom_req[100];
                strcpy(custom_req, "DELETE ");
                strcat(custom_req, path + 1);
                //setup curl options
                curl_easy_setopt(curl, CURLOPT_USERNAME, username);
                curl_easy_setopt(curl, CURLOPT_PASSWORD, password);
                curl_easy_setopt(curl, CURLOPT_URL, imap_server);
                curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, custom_req);
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0);
                //perform curl command
                res = curl_easy_perform(curl);
                if (res != CURLE_OK)
                {
                    fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
                }
                //curl cleanup
                curl_easy_cleanup(curl);

                //refresh the root directory
                filesystem->num_directories = 0;
                initialiseRootDirectories();

                return 0; //success
            }
        }
    }
    /*A directory that can't be removed*/
    else if (isDir(path))
    {
        return -EPERM; //return error code
    }
    /*The path is not to a directory*/
    else
    {
        -ENOTDIR; //return error code
    }

    return -1; //error
}

/**
 * Function that gets called when a something is written to a file.
 * Write to a mail is interpreted in two senses :-
 * 1. If the file name doesn't begin with a digit, the updated content is saved as a draft.
 * 2. If the file name begins with a digit, the file is append only, and the content is mailed to self.
 * @param path Path to the file
 * @param buffer Buffer where content is stored
 * @param size Size to be written
 * @param offset Offset for buffer
 * @param info
 * @return Number of bytes written
 */
static int do_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *info)
{
    //debug statement
    printf("\n[write called]\n\t %s path\n", path);

    /*If filename starts with digit*/
    char *temp = strstr(path + 1, "/");
    if (isdigit(*(temp + 1)))
    {
        //check if the buffer contains all the content from last mail read
        //essentially, check if only append is performed
        for (int i = 0; i < strlen(last_read_mail); i++)
        {
            if (last_read_mail[i] != buffer[i])
            {
                return -EPERM; //if not, return with proper error code
            }
        }
    }

    char draft_mail[4000]; //mail to bre draft (in RFC5322 format)

    /*Get and set current time string as per RFC5322 standard*/
    time_t t;
    time(&t);
    char *now = ctime(&t);
    char rfc_std_time[50];
    strcpy(rfc_std_time, "Date: ");
    strncat(rfc_std_time, now, 3);
    strcat(rfc_std_time, ", ");
    strncat(rfc_std_time, now + 8, 3);
    strncat(rfc_std_time, now + 4, 4);
    strncat(rfc_std_time, now + 20, 4);
    strcat(rfc_std_time, " ");
    strncat(rfc_std_time, now + 11, 8);
    strcat(rfc_std_time, " +0530\r\n");

    /*Get from and to string*/
    char from_string[50];
    strcpy(from_string, "From: <");
    //strcat(from_string, "2019csb1125.imap.app@gmail.com");
    strcat(from_string, username);
    strcat(from_string, ">\r\n");
    char to_string[50];
    strcpy(to_string, "To: <");
    //strcat(to_string, "2019csb1125.imap.app@gmail.com");
    strcat(to_string, username);
    strcat(to_string, ">\r\n");

    /*Generate message id*/
    char message_id_string[100];
    strcpy(message_id_string, "Message-ID: <");
    char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_+=";
    for (int i = 0; i < 51; i++)
    {
        strncat(message_id_string, &charset[rand() % 67], 1);
    }
    strcat(message_id_string, "@mail.gmail.com>\r\n");

    /*Genarate subject string*/
    char subject_string[250];
    strcpy(subject_string, "Subject: ");
    temp = strstr(path + 1, "/");
    temp++;
    char filename[200];
    strcpy(filename, temp);

    if (filename[0] == '.' || filename[strlen(filename) - 1] == '~')
    {
        return size;
    }

    int a = 0;
    while (a < strlen(filename))
    {
        if (filename[a] == '_')
        {
            filename[a] = ' ';
        }
        a++;
    }
    filename[strlen(filename) - 4] = '\0';
    char dirname[50];
    strcpy(dirname, "");
    sscanf(path + 1, "%[^/]s", dirname);

    //if writing from other directory than draft, change the subject to subject + next uid
    if (strcmp(dirname, "Drafts") != 0)
    {
        strcat(filename, " ");
        char num[10];
        strcpy(num, "");
        int i;
        for (i = 0; i < filesystem->num_directories; i++)
        {
            if (strcmp(filesystem->directories[i].name, "Drafts") == 0)
            {
                if (!filesystem->directories[i].in_memory)
                {
                    filesystem->directories[i].num_files = 0;
                    filesystem->directories[i].next_uid = 1;
                    initialiseDirectory("/Drafts");
                    filesystem->directories[i].in_memory = true;
                }
                break;
            }
        }
        sprintf(num, "%d", filesystem->directories[i].next_uid);
        strcat(filename, num);
    }
    strcat(subject_string, filename);
    strcat(subject_string, "\r\n");

    /*Construct draft message*/
    strcpy(draft_mail, "");
    strcat(draft_mail, rfc_std_time);
    strcat(draft_mail, to_string);
    strcat(draft_mail, from_string);
    strcat(draft_mail, message_id_string);
    strcat(draft_mail, subject_string);
    strcat(draft_mail, "\r\n");
    strcat(draft_mail, buffer);

    /*If buffer and last read mail content is same, it is considered a cp command*/
    if (strcmp(buffer, last_read_mail) == 0)
    {
        //get directory path
        char dirpath[60];
        strcpy(dirpath, "/");
        strcat(dirpath, dirname);

        //get url for the folder
        char *url = getIMAP_URL(dirpath);

        //perform curl IMAP operation
        CURL *curl;
        CURLcode res = CURLE_OK;
        curl = curl_easy_init(); //initilise curl
        //check for error
        if (!curl)
        {
            printf("\n\nError in curl_easy_init()\n\n");
            exit(-1);
        }
        long infilesize = strlen(draft_mail);
        struct upload_status upload_ctx;
        upload_ctx.bytes_read = 0;
        strcpy(upload_ctx.message, draft_mail); //copy draft mail to struct
        //set various parameters for curl operation
        curl_easy_setopt(curl, CURLOPT_USERNAME, username);
        curl_easy_setopt(curl, CURLOPT_PASSWORD, password);
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_READFUNCTION, readCallback);
        curl_easy_setopt(curl, CURLOPT_READDATA, &upload_ctx);
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
        curl_easy_setopt(curl, CURLOPT_INFILESIZE, infilesize);
        //perform the curl call
        res = curl_easy_perform(curl);
        if (res != CURLE_OK)
        {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        }
        //curl cleanup
        curl_easy_cleanup(curl);

        free(url); //free memory

        //remove the directory from memory
        for (int i = 0; i < filesystem->num_directories; i++)
        {
            if (strcmp(filesystem->directories[i].name, dirname) == 0)
            {
                filesystem->directories[i].in_memory = false;
                filesystem->directories[i].num_files = 0;
                filesystem->directories[i].next_uid = 1;
            }
        }
    }

    /*If filename starts with a digit, appending to a file implies that the file is mailed to self*/
    else if (isdigit(filename[0]))
    {
        //perform curl SMTP operation
        CURL *curl;
        CURLcode res = CURLE_OK;
        curl = curl_easy_init(); //initilise curl
        //check for error
        if (!curl)
        {
            printf("\n\nError in curl_easy_init()\n\n");
            exit(-1);
        }
        char recep[100];
        strcpy(recep, "<");
        strcat(recep, username);
        strcat(recep, ">");
        struct upload_status upload_ctx;
        struct curl_slist *recipients = NULL;
        upload_ctx.bytes_read = 0;
        strcpy(upload_ctx.message, draft_mail); //copy draft mail to struct
        //set various parameters for curl operation
        curl_easy_setopt(curl, CURLOPT_URL, smtp_server);
        curl_easy_setopt(curl, CURLOPT_USERNAME, username);
        curl_easy_setopt(curl, CURLOPT_PASSWORD, password);
        curl_easy_setopt(curl, CURLOPT_MAIL_FROM, recep);
        recipients = curl_slist_append(recipients, recep);
        curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);
        curl_easy_setopt(curl, CURLOPT_READFUNCTION, readCallback);
        curl_easy_setopt(curl, CURLOPT_READDATA, &upload_ctx);
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
        //perform the curl call
        res = curl_easy_perform(curl);
        //check for error
        if (res != CURLE_OK)
        {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        }
        //curl cleanup
        curl_easy_cleanup(curl);

        //remove INBOX from memory
        for (int i = 0; i < filesystem->num_directories; i++)
        {
            if (strcmp(filesystem->directories[i].name, "INBOX") == 0)
            {
                filesystem->directories[i].in_memory = false;
                filesystem->directories[i].num_files = 0;
                filesystem->directories[i].next_uid = 1;
            }
        }
    }
    /*If first character is not a digit, make a draft*/
    else
    {
        //get directory path
        char dirpath[60];
        strcpy(dirpath, "/");
        strcat(dirpath, dirname);

        //perform curl IMAP operation
        CURL *curl;
        CURLcode res = CURLE_OK;
        curl = curl_easy_init(); //initilise curl
        //check for error
        if (!curl)
        {
            printf("\n\nError in curl_easy_init()\n\n");
            exit(-1);
        }
        char *draft_url = getIMAP_URL(dirpath);
        long infilesize = strlen(draft_mail);
        struct upload_status upload_ctx;
        upload_ctx.bytes_read = 0;
        strcpy(upload_ctx.message, draft_mail); //copy draft mail to struct
        //set various parameters for curl operation
        curl_easy_setopt(curl, CURLOPT_USERNAME, username);
        curl_easy_setopt(curl, CURLOPT_PASSWORD, password);
        curl_easy_setopt(curl, CURLOPT_URL, draft_url);
        curl_easy_setopt(curl, CURLOPT_READFUNCTION, readCallback);
        curl_easy_setopt(curl, CURLOPT_READDATA, &upload_ctx);
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
        curl_easy_setopt(curl, CURLOPT_INFILESIZE, infilesize);
        //perform the curl call
        res = curl_easy_perform(curl);
        if (res != CURLE_OK)
        {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        }
        //curl cleanup
        curl_easy_cleanup(curl);

        free(draft_url); //free memory

        //remove Drafts from memory
        for (int i = 0; i < filesystem->num_directories; i++)
        {
            if (strcmp(filesystem->directories[i].name, dirname) == 0 || strcmp(filesystem->directories[i].name, "Drafts") == 0)
            {
                filesystem->directories[i].in_memory = false;
                filesystem->directories[i].num_files = 0;
                filesystem->directories[i].next_uid = 1;
            }
        }
    }

    return size; //return number of bytes written
}

/**
 * Function that gets called when a new file is created.
 * New file can be created only in Drafts folder.
 * @param path Path to the file
 * @param mode Access mode of the file
 * @param rdev
 * @return Success/Failure code
 */
static int do_mknod(const char *path, mode_t mode, dev_t rdev)
{
    //debug statement
    printf("\n[mknod called]\n\t %s path\n", path);

    //get Directory name
    char dirname[50];
    sscanf(path + 1, "%[^/]s", dirname);

    //if not cached, get root directories
    if (!is_cached)
    {
        filesystem->num_directories = 0;
        initialiseRootDirectories();
    }

    //search for all directories
    for (int i = 0; i < filesystem->num_directories; i++)
    {
        //when Drafts found
        if (strcmp(filesystem->directories[i].name, dirname) == 0)
        {
            //initialise the directory if not in memory or not cached
            if (!is_cached || !filesystem->directories[i].in_memory)
            {
                filesystem->directories[i].num_files = 0;
                filesystem->directories[i].next_uid = 1;
                char dirpath[60];
                strcpy(dirpath, "/");
                strcat(dirpath, dirname);
                initialiseDirectory(dirpath);
                filesystem->directories[i].in_memory = true;
            }

            //add the file to the directory
            char *filename = strstr(path + 1, "/");
            filename += 1;
            strcpy(filesystem->directories[i].files[filesystem->directories[i].num_files].disp_name, filename);
            filesystem->directories[i].num_files++;
        }
    }

    return 0; //success
}

/*Setup the functions used by fuse for implementing the filesystem*/
static struct fuse_operations operations = {
    .getattr = do_getattr,
    .readdir = do_readdir,
    .read = do_read,
    .mkdir = do_mkdir,
    .unlink = do_unlink,
    .rmdir = do_rmdir,
    .write = do_write,
    .mknod = do_mknod,
};

/**
 * Main function
 * @param argc Number of Command Line Arguments
 * @param argv All the Command Line Arguments
 * @return Exit code
 */
int main(int argc, char *argv[])
{
    /*
        USAGE :-
        fusermount -u /home/tanuj/CS303/Assignment5/test_dir
        gcc main.c -o main -lcurl `pkg-config fuse --cflags --libs`
        ./main /home/tanuj/CS303/Assignment5/test_dir -f /home/tanuj/CS303/Assignment5/credentials.config 1
    */

    /*Check for CLI*/
    if (argc < 5)
    {
        printf("\nUsage :- %s [mount point] -f [path to config file] [is_cached]\n\n", argv[0]);
        exit(-1);
    }

    //seed random time
    srand(time(NULL));

    //initialise memory for filesystem and last mail read
    filesystem = (struct root_directory *)malloc(sizeof(struct root_directory));
    last_read_mail = (char *)malloc(4000 * sizeof(char));

    //initilaise login credentails
    username = (char *)malloc(100 * sizeof(char));
    password = (char *)malloc(100 * sizeof(char));
    imap_server = (char *)malloc(200 * sizeof(char));
    smtp_server = (char *)malloc(200 * sizeof(char));

    //Get credentials from config file
    FILE *fptr;
    fptr = fopen(argv[3], "r");
    if (fptr == NULL)
    {
        printf("Cannot open file \n");
        exit(0);
    }
    char imap_port[10], smtp_port[10];
    fscanf(fptr, "%s %s\n%s %s\n%s\n%s", imap_server, imap_port, smtp_server, smtp_port, username, password);
    strcat(imap_server, ":");
    strcat(imap_server, imap_port);
    strcat(smtp_server, ":");
    strcat(smtp_server, smtp_port);
    fclose(fptr);

    //initialise the root directories for the filesystem
    initialiseRootDirectories();

    //setup fuse argumets
    char *fuse_argv[3];
    fuse_argv[0] = (char *)malloc(200);
    fuse_argv[1] = (char *)malloc(200);
    fuse_argv[2] = (char *)malloc(200);
    strcpy(fuse_argv[0], argv[0]);
    strcpy(fuse_argv[1], argv[1]);
    strcpy(fuse_argv[2], argv[2]);

    //set is_cached using CLI
    is_cached = (bool)atoi(argv[4]);

    //run the fuse file system
    int fuse_res = fuse_main(3, fuse_argv, &operations, NULL);

    //free the memory
    free(filesystem);
    free(last_read_mail);
    free(username);
    free(password);
    free(imap_server);
    free(smtp_server);
    free(fuse_argv[0]);
    free(fuse_argv[1]);
    free(fuse_argv[2]);

    return fuse_res; //return exit code
}