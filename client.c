#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#define LENGTH 2048
#define BUFFER_SZ 2048
#define STR_SIZE 32

// Global variables
static const char USERNAME_ERROR[] = "Username already exists.\n";
static const char REGISTER_SUCCESS[] = "Registered successfully.\n";
static const char LOGIN_ERROR[] = "Log in failed.\n";
static const char LOGIN_SUCCESS[] = "Logged in successfully.\n";
static const char GROUP_ERROR[] = "No valid group names found.\n";

static const char REGISTER[] = "R";
static const char LOGIN[] = "L";

static const char CREATE_GROUP[] = "cgroup";
static const char DELETE_GROUP[] = "dgroup";
static const char ENTER_GROUP[] = "egroup";
static const char SHOW_GROUPS[] = "sgroups";
static const char ADD_CONTACT[] = "acontact";
static const char DELETE_CONTACT[] = "dcontact";
static const char CONTACT_LIST[] = "clist";
static const char PERSONAL_MESSAGE[] = "pm";
static const char GROUP_MESSAGE[] = "mgroup";

volatile sig_atomic_t flag = 0;
int sockfd = 0;
char name[STR_SIZE];
char pswd[STR_SIZE];
char buff[64];
char action[STR_SIZE];
char status[STR_SIZE];
char groups_list[2048];
char groups[1024];

void str_overwrite_stdout() {
  printf("%s", "> ");
  fflush(stdout);
}

void str_trim_lf (char* arr, int length) {
  int i;
  for (i = 0; i < length; i++) { // trim \n
    if (arr[i] == '\n') {
      arr[i] = '\0';
      break;
    }
  }
}

void trim_trailing_spaces(char * str) {
    int index, i;

    index = -1;
    i = 0;
    while(str[i] != '\0') {
        if(str[i] != ' ' && str[i] != '\t' && str[i] != '\n') {
            index= i;
        }
        i++;
    }

    str[index + 1] = '\0';
}

void catch_ctrl_c_and_exit(int sig) {
    flag = 1;
}

void send_msg_handler() {
  char message[LENGTH] = {};
	char buffer[LENGTH + STR_SIZE] = {};

  while(1) {
  	str_overwrite_stdout();
    fgets(message, LENGTH, stdin);
    str_trim_lf(message, LENGTH);

    if (strcmp(message, "exit") == 0) {
			break;
    } else if(strcmp(message, CONTACT_LIST) == 0 || strncmp(message, ADD_CONTACT, strlen(ADD_CONTACT)) == 0
        || strncmp(message, DELETE_GROUP, strlen(DELETE_GROUP)) == 0
        || strncmp(message, CREATE_GROUP, strlen(CREATE_GROUP)) == 0
        || strncmp(message, ENTER_GROUP, strlen(ENTER_GROUP)) == 0
        || strncmp(message, PERSONAL_MESSAGE, strlen(PERSONAL_MESSAGE)) == 0
        || strncmp(message, DELETE_GROUP, strlen(DELETE_GROUP)) == 0
        || strncmp(message, GROUP_MESSAGE, strlen(GROUP_MESSAGE)) == 0
        || strncmp(message, DELETE_CONTACT, strlen(DELETE_CONTACT)) == 0
        || strcmp(message, SHOW_GROUPS) == 0) {
      send(sockfd, message, strlen(message), 0);
    } else {
      sprintf(buffer, "%s: %s\n", name, message);
      send(sockfd, buffer, strlen(buffer), 0);
    }

		bzero(message, LENGTH);
    bzero(buffer, LENGTH + STR_SIZE);
  }
  catch_ctrl_c_and_exit(2);
}

void recv_msg_handler() {
	char message[LENGTH] = {};
  while (1) {
		int receive = recv(sockfd, message, LENGTH, 0);
    if (receive > 0) {
      printf("%s", message);
      str_overwrite_stdout();
    } else if (receive == 0) {
			break;
    } else {
			// -1
		}
		memset(message, 0, sizeof(message));
  }
}

int main(int argc, char **argv){
	if(argc != 2){
		printf("Usage: %s <port>\n", argv[0]);
		return EXIT_FAILURE;
	}

	char *ip = "127.0.0.1";
	int port = atoi(argv[1]);

	signal(SIGINT, catch_ctrl_c_and_exit);

	printf("Register or Login? (R/L): ");
	fgets(action,STR_SIZE,stdin);
	str_trim_lf(action, strlen(action));
	if (strcmp(action,REGISTER)!=0 && strcmp(action,LOGIN)!=0){
		printf("Invalid input. Only R or L are accepted\n");
		return EXIT_FAILURE;
	}

  struct sockaddr_in server_addr;

	/* Socket settings */
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = inet_addr(ip);
  server_addr.sin_port = htons(port);


  // Connect to Server
  int err = connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
  if (err == -1) {
		printf("ERROR: connect\n");
		return EXIT_FAILURE;
	}

  // Sending action to server
  send(sockfd, action, STR_SIZE, 0);

	if (strcmp(action,REGISTER)==0){

		//Register
		printf("Registering...\n");
		//Enter username
		printf("Please enter a username (max 30 characters)\n");
		fgets(name, STR_SIZE, stdin);

  	str_trim_lf(name, strlen(name));
    trim_trailing_spaces(name);

		if (strlen(name) > STR_SIZE || strlen(name) < 2){
			printf("Name must be less than 30 and more than 2 characters.\n");
			return EXIT_FAILURE;
		}

    send(sockfd, name, STR_SIZE, 0);

		// password
		printf("Please enter a password (max 30 characters)\n");
		fgets(pswd, STR_SIZE, stdin);

  	str_trim_lf(pswd, strlen(pswd));
    trim_trailing_spaces(pswd);

		if (strlen(pswd) > STR_SIZE || strlen(pswd) < 2){
			printf("Password must be less than 30 and more than 2 characters.\n");
			return EXIT_FAILURE;
		}

    send(sockfd, pswd, STR_SIZE, 0);

    recv(sockfd, groups_list, BUFFER_SZ, 0);
    if(strcmp(groups_list,USERNAME_ERROR)!=0) {
      printf("Please choose groups to join out of the following: \n");
      printf("(Type in names of groups, COMMA SEPARATED)\n");

      printf("%s\n", groups_list);
  		fgets(groups, 1024, stdin);

    	str_trim_lf(groups, strlen(groups));
      trim_trailing_spaces(groups);

      if (strlen(groups) > 1024 || strlen(pswd) < 2){
        printf("Groups must be less than 1024 and more than 2 characters.\n");
        return EXIT_FAILURE;
      }
      send(sockfd, groups, 1024, 0);

      recv(sockfd, status, STR_SIZE, 0);
    } else {
      printf(USERNAME_ERROR);
      printf("Register failed.\n");
      return EXIT_FAILURE;
    }

    if(strcmp(status,GROUP_ERROR)==0){
      printf(GROUP_ERROR);
      printf("Register failed.\n");
      return EXIT_FAILURE;
    } else if(strncmp(status,REGISTER_SUCCESS,24)==0) {
      printf("%s\n", status );
    }

	}
	else if (strcmp(action,LOGIN)==0){
		//Login

		printf("Logging in...\n");
		//Enter username
		printf("Please enter your username (max 30 characters)\n");
		fgets(name, STR_SIZE, stdin);

  	str_trim_lf(name, strlen(name));
    trim_trailing_spaces(name);

		if (strlen(name) > STR_SIZE || strlen(name) < 2){
			printf("Name must be less than 30 and more than 2 characters.\n");
			return EXIT_FAILURE;
		}

    send(sockfd, name, STR_SIZE, 0);

		// password
		printf("Please enter your password (max 30 characters)\n");
		fgets(pswd, STR_SIZE, stdin);

  	str_trim_lf(pswd, strlen(pswd));
    trim_trailing_spaces(pswd);

		if (strlen(pswd) > STR_SIZE || strlen(pswd) < 2){
			printf("Password must be less than 30 and more than 2 characters.\n");
			return EXIT_FAILURE;
		}

    send(sockfd, pswd, STR_SIZE, 0);

    recv(sockfd, status, STR_SIZE, 0);

    if(strcmp(status,LOGIN_ERROR)==0) {
      printf(LOGIN_ERROR);
      return EXIT_FAILURE;
    } else if(strcmp(status, LOGIN_SUCCESS)==0) {
      printf(LOGIN_SUCCESS);
    }
	}


	printf("=================== WELCOME TO THE CHATROOM =====================\n");
  printf("AVAILABLE OPTIONS:\n");
  printf("1. Create group (%s <group_name>)\n", CREATE_GROUP);
  printf("2. Delete group (%s <group_name>)\n", DELETE_GROUP);
  printf("3. Enter group (%s <group_name>)\n", ENTER_GROUP);
  printf("4. Show all groups (%s)\n", SHOW_GROUPS);
  printf("5. Add contact (%s <contact_name>)\n", ADD_CONTACT);
  printf("6. Delete contact (%s <contact_name>)\n", DELETE_CONTACT);
  printf("7. Show contact list (%s)\n", CONTACT_LIST);
  printf("8. Send personal message to contact (%s <contact_name> <message>)\n",PERSONAL_MESSAGE);
  printf("9. Send message to group (%s <group_name> <message>)\n",GROUP_MESSAGE);
  printf("=================================================================\n");

	pthread_t send_msg_thread;
  if(pthread_create(&send_msg_thread, NULL, (void *) send_msg_handler, NULL) != 0){
		printf("ERROR: pthread\n");
    return EXIT_FAILURE;
	}

	pthread_t recv_msg_thread;
  if(pthread_create(&recv_msg_thread, NULL, (void *) recv_msg_handler, NULL) != 0){
		printf("ERROR: pthread\n");
		return EXIT_FAILURE;
	}

	while (1){
		if(flag){
			printf("\nBye\n");
			break;
    }
	}

	close(sockfd);

	return EXIT_SUCCESS;
}
