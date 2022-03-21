Submitter name: Tanuj Maheshwari
Roll No.: 2019CSB1125
Course: CS303 - Operating Systems

=================================


1. What does this program do

This program mounts a GMail mailbox to a remote mountpoint on a Linux system. The folders in the mailbox (like All, INBOX, Drafts, etc) are represented as directories under the mountpoint, and the mails within those folders are described as the files in the subsystem. The file content is the text part of the mail, and the file name is the subject of the file. Basic commands like cd, ls, mkdir, cp, etc can be used from the terminal to manipulate the mailbox, and the files can be read from/written to.


2. A description of how this program works (i.e. its logic)

The filesystem is built using FUSE, implemented in C using the libfuse library. The mail connection is done using IMAP (for reading/manipulating mailboxes) and SMTP (for writing), implemented using libcurl library.
The mailbox is mounted to a subdirectory (given by the user), which can then be accessed through basic terminal commands like cd, ls, cat, etc.
Each folder in the mailbox (line INBOX, Drafts, Starred, or labels like TO_FOLLOWUP, etc) are mapped to directories underthe mount point (which acts as a reletive root).
Each mail is mapped to a text file in the respective folder. The content of the text file is the text part of the mail body. This file can be read, written to, deleted, modified, etc using commands like cat, or through editors like vi.
These changes map to some respective mailbox manipulation functions, and performing the commands will reflect the corresponding changes onto the mailbox.


The program allows two interfaces to run :-

1. is_cached == 1 :- The filenames and directory names are cached in main memory. This makes the cd and ls commands run very fast (as compared to when not cached). But external changes in the mailbox may not immediately be visible.
2. is_cached == 0 :- No caching happens, and all the commands take a lot of time to run. But extrenal changes in the mailbox will be visible.


Following are the FUSE function defined (with breif description) :-

1. getattr - Get the attributes (mode, size, etc) of a file/folder.
2. readdir - Get the files/directories in a directory.
3. read - Read the file (mail) content.
4. mkdir - Make directory (label) at the root level.
5. unlink - Called when a file is deleted.
6. rmdir - Remove a directory (label).
7. write - Called when writing to a file.
8. mknod - Create a new file.


The following commands can be used on the mounted subsytem :-

1. cd - change directory.
        Usage -> $ cd INBOX/
2. ls - list the files in the directory (all the mails listed as text files). Names of the mails will be the subjects of the respective mails (with spaces replaced by an _).
        Usage -> $ ls
3. cat - shows the content of the file within the directory. 
        Usage -> $ cat file_name.txt
4. echo - used to make a new file in a directory. 
        Usage -> $ echo 'Mail Content' > file_name.txt
5. rm - delete the given file
        Usage -> $ rm file_name.txt
6. rmdir - delete the directory (mail folder)
        Usage -> $ rmdir TO_FOLLOWUP
7. cp - copy the file from the current folder to a different folder
        Usage -> cp file_name.txt ../TO_FOLLOWUP
8. vi (or vim) - for reading/writing to a file
        Usage -> vi file_name.txt



NOTES :-

ONLY GMAIL SERVER CAN BE USED

1. [Gmail]/Starred is mapped to Starred, [Gmail]/All Mail is mapped to All.
2. Root can have atmost 100 directories, each directory can have atmost 500 mails.
3. Name of directory is atmax 49 characters long, name of file at max 199 character long. Each file must be less than 4000 characters long.
4. Files can't be deleted from All and Trash.
5. Labels can be created with any name (atmost 2 characters long).
6. Only those labels can be deleted whose 2nd character is an uppercase alphabet.
7. Editing a file implies that the original file will be deleted, and a new file will be created in the same directory with the new content.**
8. The file name for a newly created file can differ from that given by the user. This is because the next uid is appended to the file name so that same name files are not created. (The subject for the mail will also have the uid appended to it).
9. Command mkdir can be used only on the root directory (because GMail only allows labels at root level (sublabels are also labels at root level)).
10. New files can be created only inside a level 1 directory (as files only exist inside folders).
11. Files can be copied only to level 1 directories.
12. Files begining with digit can't be deleted.
13. Contents of files greater than 4000 characters, and for files with garbage characters won't be read.

**-> vi causes various problems for reading/writing as a lot depends on the vim configuration. 
Eg, When writing to a file, vi first deletes the old file and then creates a new file. This causes problems in writing to append only files (which can't be deleted).


3. How to compile and run this program

Before running, first the GMail account must have IMAP (&SMTP) enabled. This can be enabled in GMail settings.
Further, the Google account must allow less secure apps to connect. This can be enabled in Google account settings.

Also, before running, amke sure that libcurl, libfuse and pkg-config are installed. To install, :-
    $ sudo apt-get install libfuse -y libfuse-dev
    $ sudo apt-get install libcurl-openssl-dev
    $ sudo apt-get install pkg-config

To compile and run the program :-

    a. Open a terminal in the directory of 'main.c' file.

    b. Unmount the mountpoint :
            $ fusermount -u <path to mountpoint>
        (This might show error, if mountpoint is not mounted, but is not relevant (this is more of a assertion)).
    
    b. Compile the program, using :
            $ gcc main.c -o main -lcurl `pkg-config fuse --cflags --libs`

    c. To run the executable :
            $ ./main <path to mountpoint> -f <path to config file> [is_cached]

    d. The program prints debug statements during execution.


Format for config file :-

```
[IMAP server] [IMAP port]
[SMTP server] [SMTP port]
[username]
[password]
```
Standard values (for GMail) :-
IMAP Server - imaps://imap.gmail.com
IMAP Port - 993
SMTP Server - smtps://smtp.gmail.com
SMTP Server - 465
username - (email id)
password - (your google account password)



4. Snapshot of a sample run

Client Side :-

```
tanuj@LAPTOP-B8BENHVH:~/CS303/Assignment5$ cd test_dir/
tanuj@LAPTOP-B8BENHVH:~/CS303/Assignment5/test_dir$ ls
All  Drafts  INBOX  Important  Sent  Spam  Starred  Trash  label4
tanuj@LAPTOP-B8BENHVH:~/CS303/Assignment5/test_dir$ cd INBOX/
tanuj@LAPTOP-B8BENHVH:~/CS303/Assignment5/test_dir/INBOX$ ls
01_Test_for_append_only_files_51.txt  File_4_for_fuse.txt       SMTP_example_message.txt
Create_from_filesystem_51_51.txt      Remove_label_test_47.txt  Test_for_file_2.txt
tanuj@LAPTOP-B8BENHVH:~/CS303/Assignment5/test_dir/INBOX$ cat File_4_for_fuse.txt
Lets have a test. This is in one line.
This is line 2.

The above line was empty (this is line 4).
Great.
tanuj@LAPTOP-B8BENHVH:~/CS303/Assignment5/test_dir/INBOX$ cd ../
tanuj@LAPTOP-B8BENHVH:~/CS303/Assignment5/test_dir$ mkdir LABEL5
tanuj@LAPTOP-B8BENHVH:~/CS303/Assignment5/test_dir$ ls
All  Drafts  INBOX  Important  LABEL5  Sent  Spam  Starred  Trash  label4
tanuj@LAPTOP-B8BENHVH:~/CS303/Assignment5/test_dir$ cd INBOX/
tanuj@LAPTOP-B8BENHVH:~/CS303/Assignment5/test_dir/INBOX$ cp SMTP_example_message.txt ../LABEL5/
tanuj@LAPTOP-B8BENHVH:~/CS303/Assignment5/test_dir/INBOX$ cd ../LABEL5/
tanuj@LAPTOP-B8BENHVH:~/CS303/Assignment5/test_dir/LABEL5$ ls
SMTP_example_message_51.txt
tanuj@LAPTOP-B8BENHVH:~/CS303/Assignment5/test_dir/LABEL5$ cd ../INBOX/
tanuj@LAPTOP-B8BENHVH:~/CS303/Assignment5/test_dir/INBOX$ rm Test_for_file_2.txt
tanuj@LAPTOP-B8BENHVH:~/CS303/Assignment5/test_dir/INBOX$ ls
01_Test_for_append_only_files_51.txt  File_4_for_fuse.txt       SMTP_example_message.txt
Create_from_filesystem_51_51.txt      Remove_label_test_47.txt
tanuj@LAPTOP-B8BENHVH:~/CS303/Assignment5/test_dir/INBOX$ rm 01_Test_for_append_only_files_51.txt
rm: cannot remove '01_Test_for_append_only_files_51.txt': Operation not permitted
tanuj@LAPTOP-B8BENHVH:~/CS303/Assignment5/test_dir/INBOX$ cd ../
tanuj@LAPTOP-B8BENHVH:~/CS303/Assignment5/test_dir$ rmdir LABEL5/
tanuj@LAPTOP-B8BENHVH:~/CS303/Assignment5/test_dir$ ls
All  Drafts  INBOX  Important  Sent  Spam  Starred  Trash  label4
tanuj@LAPTOP-B8BENHVH:~/CS303/Assignment5/test_dir$
```

Server Side :-

```
tanuj@LAPTOP-B8BENHVH:~/CS303/Assignment5$ fusermount -u /home/tanuj/CS303/Assignment5/test_dir
fusermount: entry for /home/tanuj/CS303/Assignment5/test_dir not found in /etc/mtab
tanuj@LAPTOP-B8BENHVH:~/CS303/Assignment5$ gcc main.c -o main -lcurl `pkg-config fuse --cflags --libs`
tanuj@LAPTOP-B8BENHVH:~/CS303/Assignment5$ ./main /home/tanuj/CS303/Assignment5/test_dir -f /home/tanuj/CS303/Assignment5/credentials.config 1

[getattr called]
         / path

[getattr called]
         / path

[readdir called]
         / path

[getattr called]
         /INBOX path

[getattr called]
         /All path

[getattr called]
         /Drafts path

[getattr called]
         /Important path

[getattr called]
         /Sent path

[getattr called]
         /Spam path

[getattr called]
         /Starred path

[getattr called]
         /Trash path

[getattr called]
         /label4 path

[getattr called]
         / path

[readdir called]
         / path

[getattr called]
         /INBOX path

[getattr called]
         / path

[getattr called]
         / path

[getattr called]
         /INBOX path

[getattr called]
         /INBOX path

[readdir called]
         /INBOX path

[getattr called]
         /INBOX/Test_for_file_2.txt path

[getattr called]
         /INBOX/File_4_for_fuse.txt path

[getattr called]
         /INBOX/SMTP_example_message.txt path

[getattr called]
         /INBOX/Remove_label_test_47.txt path

[getattr called]
         /INBOX/01_Test_for_append_only_files_51.txt path

[getattr called]
         /INBOX/Create_from_filesystem_51_51.txt path

[getattr called]
         /INBOX path

[readdir called]
         /INBOX path

[getattr called]
         / path

[getattr called]
         /INBOX path

[getattr called]
         /INBOX/File_4_for_fuse.txt path

[getattr called]
         /INBOX path

[getattr called]
         /INBOX/File_4_for_fuse.txt path

[read called]
         /INBOX/File_4_for_fuse.txt path

[getattr called]
         /INBOX/File_4_for_fuse.txt path

[read called]
         /INBOX/File_4_for_fuse.txt path

[getattr called]
         / path

[getattr called]
         /INBOX path

[getattr called]
         / path

[getattr called]
         /LABEL5 path

[mkdir called]
         /LABEL5 path

[getattr called]
         /LABEL5 path

[getattr called]
         / path

[readdir called]
         / path

[getattr called]
         /INBOX path

[getattr called]
         /All path

[getattr called]
         /Drafts path

[getattr called]
         /Important path

[getattr called]
         /Sent path

[getattr called]
         /Spam path

[getattr called]
         /Starred path

[getattr called]
         /Trash path

[getattr called]
         /label4 path

[getattr called]
         /LABEL5 path

[getattr called]
         / path

[readdir called]
         / path

[getattr called]
         /INBOX path

[getattr called]
         / path

[getattr called]
         / path

[getattr called]
         /INBOX path

[getattr called]
         /INBOX path

[readdir called]
         /INBOX path

[getattr called]
         / path

[getattr called]
         /INBOX path

[getattr called]
         /INBOX/SMTP_example_message.txt path

[getattr called]
         / path

[getattr called]
         /INBOX path

[readdir called]
         / path

[getattr called]
         / path

[getattr called]
         /LABEL5 path

[getattr called]
         /INBOX path

[getattr called]
         /LABEL5 path

[getattr called]
         /INBOX/SMTP_example_message.txt path

[getattr called]
         /LABEL5/SMTP_example_message.txt path

[getattr called]
         /INBOX/SMTP_example_message.txt path

[getattr called]
         /LABEL5 path

[getattr called]
         /LABEL5/SMTP_example_message.txt path

[mknod called]
         /LABEL5/SMTP_example_message.txt path

[getattr called]
         /LABEL5/SMTP_example_message.txt path

[read called]
         /INBOX/SMTP_example_message.txt path

[write called]
         /LABEL5/SMTP_example_message.txt path

[getattr called]
         /INBOX/SMTP_example_message.txt path

[getattr called]
         / path

[getattr called]
         /INBOX path

[readdir called]
         / path

[getattr called]
         /LABEL5 path

[getattr called]
         / path

[getattr called]
         /LABEL5 path

[readdir called]
         /LABEL5 path

[getattr called]
         /LABEL5/SMTP_example_message_51.txt path

[getattr called]
         / path

[getattr called]
         /LABEL5 path

[readdir called]
         / path

[getattr called]
         /INBOX path

[getattr called]
         / path

[getattr called]
         /INBOX path

[readdir called]
         /INBOX path

[getattr called]
         / path

[getattr called]
         /INBOX path

[getattr called]
         /INBOX/Test_for_file_2.txt path

[getattr called]
         /INBOX path

[getattr called]
         /INBOX/Test_for_file_2.txt path

[unlink called]
         /INBOX/Test_for_file_2.txt path
* 1 FETCH (FLAGS (\Deleted) UID 4)
* 1 EXPUNGE
* 5 EXISTS

[getattr called]
         /INBOX path

[readdir called]
         /INBOX path

[getattr called]
         /INBOX/File_4_for_fuse.txt path

[getattr called]
         /INBOX/SMTP_example_message.txt path

[getattr called]
         /INBOX/Remove_label_test_47.txt path

[getattr called]
         /INBOX/01_Test_for_append_only_files_51.txt path

[getattr called]
         /INBOX/Create_from_filesystem_51_51.txt path

[getattr called]
         /INBOX path

[readdir called]
         /INBOX path

[getattr called]
         / path

[getattr called]
         /INBOX path

[getattr called]
         /INBOX/01_Test_for_append_only_files_51.txt path

[unlink called]
         /INBOX/01_Test_for_append_only_files_51.txt path

[getattr called]
         / path

[getattr called]
         /INBOX path

[readdir called]
         / path

[getattr called]
         /LABEL5 path

[getattr called]
         / path

[getattr called]
         / path

[getattr called]
         /INBOX path

[getattr called]
         / path

[readdir called]
         / path

[getattr called]
         /LABEL5 path

[getattr called]
         / path

[rmdir called]
         /LABEL5 path

[getattr called]
         / path

[readdir called]
         / path

[getattr called]
         /INBOX path

[getattr called]
         /All path

[getattr called]
         /Drafts path

[getattr called]
         /Important path

[getattr called]
         /Sent path

[getattr called]
         /Spam path

[getattr called]
         /Starred path

[getattr called]
         /Trash path

[getattr called]
         /label4 path
```