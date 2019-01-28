// Shang-Yi Yang
// CPSC 4510
// MyProxy.cpp
// This class program implements a simple proxy server

#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>

#define BACKLOG 10

void sigchld_handler(int s) {
  // waitpid() might overwrite errno, so we save and restore it:
  int saved_errno = errno;

  while(waitpid(-1, NULL, WNOHANG) > 0);

  errno = saved_errno;
}


// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa) {
  if (sa->sa_family == AF_INET) {
    return &(((struct sockaddr_in*)sa)->sin_addr);
  }

  return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(int argc, char *argv[]) {
  int sockfd, new_fd;
  struct addrinfo hints, *servinfo, *p; 
  struct sockaddr_storage their_addr;
  socklen_t sin_size;
  struct sigaction sa;
  struct hostent * host;
  int yes = 1;
  char s[INET6_ADDRSTRLEN];
  int rv;

  if (argc < 2) {
    perror("please provide port number");
    exit(0);
  }

  
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE; // use my IP

  if ((rv = getaddrinfo(NULL, argv[1], &hints, &servinfo)) != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
    return 1;
  }

  for (p = servinfo; p != NULL; p = p->ai_next) {
    if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
      perror("server: socket");
      continue;
    }
    
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
      perror("setsockopt");
      exit(1);
    }

    if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
      close(sockfd);
      perror("server: bind");
      continue;
    }

    break;
  }

  freeaddrinfo(servinfo);

  if (p == NULL) {
    fprintf(stderr, "server: failed to bind\n");
    exit(1);
  }

  if (listen(sockfd, BACKLOG) == -1) {
    perror("listen");
    exit(1);
  }

  // Referenced from Beej's guide to network programming
  sa.sa_handler = sigchld_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  if (sigaction(SIGCHLD, &sa, NULL) == -1) {
    perror("sigaction");
    exit(1);
  }

  printf("Waiting for connections...\n");
  
  while (1) {
    sin_size = sizeof their_addr;
    new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
    if (new_fd == -1) {
      perror("accept");
      continue;
    }

    inet_ntop(their_addr.ss_family,
              get_in_addr((struct sockaddr *)&their_addr),
              s, sizeof s);

    if (!fork()) {
      struct sockaddr_in host_addr;
      char buffer[1024], req[500], uri[500], ver[24];
      int defaultPort = 1, newsockfd1, port = 80, i, sockfd1, n;
      char * temp = NULL;

      bzero((char *) buffer, 1024);

      recv(new_fd, buffer, 1024, 0);

      sscanf(buffer, "%s %s %s", req, uri, ver);

      if (strncmp(ver, "HTTP/1.0", 8) != 0 ||
          strncmp(req, "GET", 3) != 0 ||
          strncmp(uri, "http://", 7) !=0) {
        close(sockfd);
        send(new_fd, "500: Internal Error\r\n", 22, 0);
        close(new_fd);
        exit(0);
      }

      for (i = 7; i < strlen(uri); i++) {
        if (uri[i] == ':') {
          defaultPort = 0;
          break;
        }
      }

      temp = strtok(uri, "//");
      if (!defaultPort) {
        temp = strtok(NULL, ":");
      } else {
        temp = strtok(NULL, "/");
      }

      sprintf(uri, "%s", temp);
      host = gethostbyname(uri);

      if (!defaultPort) {
        temp = strtok(NULL, "/");
        port = atoi(temp);
      }
      temp = strtok(NULL, "/");

      char path[100] = "";
      
      while (temp != NULL) {
        strcat(path, "/");
        strcat(path, temp);
        temp =strtok(NULL, "/");
      }
      
      bzero((char *) &host_addr, sizeof(host_addr));
      host_addr.sin_port = htons(port);
      host_addr.sin_family = AF_INET;

      bcopy((char *) host->h_addr, (char *) &host_addr.sin_addr.s_addr, host->h_length);

      sockfd1 = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
      newsockfd1 = connect(sockfd1, (struct sockaddr *) &host_addr, sizeof(struct sockaddr));

      if (newsockfd1 == -1) {
        perror("error connecting to remote server");
        exit(0);
      }

      bzero((char *) buffer, 1024);
      if (strlen(path) > 0) {
        sprintf(buffer, "GET %s %s\r\nHost: %s\r\nConnection: close\r\n\r\n", path, ver, uri);
      } else {
        sprintf(buffer, "GET / %s\r\nHost: %s\r\nConnection: close\r\n\r\n", ver, uri);
      }
      n = send(sockfd1, buffer, strlen(buffer), 0);
      
      if (n == -1) {
        perror("error writing remote");
      } else {
        do {
          bzero((char *) buffer, 1024);
          n = recv(sockfd1, buffer, 1024, 0);
          if (n > 0) {
            send(new_fd, buffer, n, 0);
          }
        } while (n > 0);
      }
      
      close(sockfd1);
      close(sockfd);
      close(new_fd);
      exit(0);
    }
    close(new_fd);
  }
  return 0;
}
