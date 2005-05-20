#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/soundcard.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define PROG "mope"

#define HOST "blue"
#define PORT "14665"

#define PATH "/var/files/songs"

#define ICES_CONF "/home/eric/src/mope/ices-stdin.xml"

#define PAUSE 0
#define STOP  1
#define PREV  2
#define NEXT  3
#define PLAY  4
#define JUMP  5
#define TITLE 6
#define ADD   7
#define EXIT  8
#define LIST  9

typedef struct _list {
	struct _list *prev;
	struct _list *next;
	void *data;
} list;

static list *
list_new(void *data)
{
	list *l = malloc(sizeof (list));
	l->prev = NULL;
	l->next = NULL;
	l->data = data;
	return (l);
}

static unsigned int
list_length(list *l)
{
	unsigned int c = 0;

	while (l) {
		l = l->next;
		c++;
	}

	return (c);
}

static void *
list_nth(list *l, int n)
{
	while (l && n) {
		l = l->next;
		n--;
	}
	if (l) return l->data;
	return (NULL);
}

static list *
list_append(list *l, void *data)
{
	list *s = l;

	if (!s) return list_new(data);

	while (s->next) s = s->next;
	s->next = list_new(data);
	s->next->prev = s;

	return (l);
}

static list *
list_prepend(list *l, void *data)
{
	list *s = list_new(data);
	s->next = l;
	if (l)
		l->prev = s;
	return (s);
}

static list *
list_insert(list *l, void *data, int pos)
{
	list *c = l, *t;
	int len;

	if (pos <= 0)
		return (list_prepend(l, data));

	len = list_length(l);
	if (pos > len)
		return (list_append(l, data));

	while (--pos)
		c = c->next;
	t = c->next;
	c->next = list_new(data);
	c->next->prev = c;
	c->next->next = t;
	t->prev = c->next;

	return (l);
}

static list *
list_remove(list *l, void *data)
{
	list *s = l, *p = NULL;

	if (!s) return NULL;
	if (s->data == data) {
		p = s->next;
		if (p)
			p->prev = NULL;
		free(s);
		return (p);
	}
	while (s->next) {
		p = s;
		s = s->next;
		if (s->data == data) {
			p->next = s->next;
			if (p->next)
				p->next->prev = p;
			free(s);
			return (l);
		}
	}
	return (l);
}

static list *
read_playlist(list *play, char *path)
{
	DIR *d;
	struct dirent *ent;

	if (!(d = opendir(path)))
		return (play);
	while ((ent = readdir(d))) {
		struct stat s;
		char *x;
		if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
			continue;
		x = malloc(strlen(path) + strlen(ent->d_name) + 1);
		sprintf(x, "%s/%s", path, ent->d_name);
		if (stat(x, &s))
			free(x);
		else if (S_ISREG(s.st_mode))
			play = list_append(play, x);
		else if (S_ISDIR(s.st_mode))
			play = read_playlist(play, x);
	}
	closedir(d);

	return (play);
}

static list *
rand_playlist(list *playlist)
{
	list *nl = NULL;
	int l = list_length(playlist);

	srand(time(NULL));

	while (l) {
		int k = rand() % l--;
		char *x = list_nth(playlist, k);
		playlist = list_remove(playlist, x);
		nl = list_append(nl, x);
	}

	return (nl);
}

static char *
lower(char *w)
{
	char *x = strdup(w);
	char *s = x;
	while (*s) {
		*s = tolower(*s);
		s++;
	}
	return (x);
}

static list *
find_song(list *playlist, list *cur_song, char *words)
{
	list *l = cur_song->next;

	while (l) {
		char *t = lower(l->data);
		char *w = lower(words);
		char *d = strtok(w, " ");
		while (d) {
			if (!strstr(t, d))
				break;
			d = strtok(NULL, " ");
		}
		free(w);
		free(t);
		if (!d)
			break;
		l = l->next;
	}

	if (l)
		return (l);

	l = playlist;

	while (l) {
		char *t = lower(l->data);
		char *w = lower(words);
		char *d = strtok(w, " ");
		while (d) {
			if (!strstr(t, d))
				break;
			d = strtok(NULL, " ");
		}
		free(w);
		free(t);
		if (!d)
			break;
		l = l->next;
	}

	return (l);
}

static int
create_socket()
{
	int listenfd;
	const int on = 1;
	struct addrinfo hints, *res, *ressave;

	memset(&hints, 0, sizeof (struct addrinfo));
	hints.ai_flags = AI_PASSIVE;
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(HOST, PORT, &hints, &res) != 0) {
		perror("getaddrinfo");
		return (-1);
	}
	ressave = res;
	do {
		listenfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (listenfd < 0)
			continue;
		setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof (on));
		if (bind(listenfd, res->ai_addr, res->ai_addrlen) == 0)
			break;
		close(listenfd);
	} while ((res = res->ai_next));

	if (!res)
		return (-1);

	if (listen(listenfd, 1024) != 0) {
		perror("listen");
		return (-1);
	}

	freeaddrinfo(ressave);
	return (listenfd);
}

static void
process(int argc, char **argv)
{
	struct sockaddr_in saddr;
	struct hostent *hp;
	int forcetitle = 0;
	int fd;

	char *x;
	char cmd = -1;
	char *t = NULL;
	int s, i;

	if (argc < 2)
		return;

	x = argv[1];
	while (*x == '-') x++;
	switch (*x) {
	case 'u':
		cmd = PAUSE;
		break;
	case 's':
		cmd = STOP;
		break;
	case 'r':
		cmd = PREV;
		break;
	case 'f':
		cmd = NEXT;
		break;
	case 'p':
		cmd = PLAY;
		break;
	case 'j':
		cmd = JUMP;
		s = 0;
		for (i = 2; i < argc; i++)
			s += strlen(argv[i]) + 1;
		if (!s)
			return;
		t = malloc(s);
		*t = 0;
		for (i = 2; i < argc; i++) {
			strcat(t, argv[i]);
			t[strlen(t)] = ' ';
		}
		t[s - 1] = 0;
		break;
	case 'T':
		forcetitle = 1;
	case 't':
		cmd = TITLE;
		break;
	case 'a':
		cmd = ADD;
		if (argc < 3)
			return;
		s = strlen(argv[2]) + 1;
		t = strdup(argv[2]);
		break;
	case 'x':
		cmd = EXIT;
		break;
	case 'l':
		cmd = LIST;
		break;
	default:
		printf("unknown option %c\n\n", *x);
	case 'h':
		printf("Usage: %s [-][usrfpjth]\n", argv[0]);
		printf(" u: pause/unpause\n");
		printf(" p: play\n");
		printf(" s: stop\n");
		printf(" r: rewind\n");
		printf(" f: forward\n");
		printf(" j: jump\n");
		printf(" t: title of current song\n");
		printf(" T: title of current song (even if stopped)\n");
		printf(" l: list of nearest 11 songs in playlist\n");
		printf(" a: add a song to the playlist (give full path)\n");
		printf(" x: exit (kill the daemon)\n");
		printf(" h: this help\n");
		printf("\nJump will try to figure out which song to jump to\n");
		printf("based on the arguments. It finds the first song in\n");
		printf("the playlist that has all arguments (case insensitive)\n");
		printf("in its filename.\n");
		return;
	}

	if (!(hp = gethostbyname(HOST)))
		return;

	memset(&saddr, 0, sizeof (struct sockaddr_in));
	saddr.sin_port = htons(atoi(PORT));
	memcpy(&saddr.sin_addr, hp->h_addr, hp->h_length);
	saddr.sin_family = hp->h_addrtype;

	if ((fd = socket(hp->h_addrtype, SOCK_STREAM, 0)) < 0)
		return;
	if (connect(fd, (struct sockaddr *)&saddr, sizeof (saddr)) < 0)
		return;

	write(fd, &cmd, 1);
	if (t) {
		write(fd, &s, sizeof (s));
		write(fd, t, s);
	}
	if (cmd == TITLE || cmd == LIST) {
		if (read(fd, &cmd, 1) == 1) {
			if (cmd || forcetitle)
				while (read(fd, &cmd, 1) == 1)
					printf("%c", cmd);
		}
	}
	free(t);
	close(fd);
}

static pid_t chld = -1;

static void
sigint(int sig)
{
	if (chld > 0) {
		kill(chld, SIGCONT);
		kill(chld, SIGTERM);
	}
	exit(0);
}

static inline void
kill_chld(int paused)
{
	kill(chld, SIGTERM);
	if (paused)
		kill(chld, SIGCONT);
	usleep(20000);
}


static int
process_cmd(int sock,
			list **cur_song, list **playlist,
			int *stopped, int *paused, int *mod)
{
	fd_set set;
	struct timeval tv, *stv;
	struct sockaddr_in saddr;
	int len = sizeof (saddr), pos;
	int fd;
	char cmd;
	char *title;
	list *tmp_list;

	if (*stopped) {
		stv = NULL;
	} else {
		tv.tv_sec = 0;
		tv.tv_usec = 100000;
		stv = &tv;
	}

	FD_ZERO(&set);
	FD_SET(sock, &set);
	if ((select(sock + 1, &set, NULL, NULL, stv) <= 0) ||
		((fd = accept(sock, (struct sockaddr *)&saddr, &len)) == -1))
		return (0);

	read(fd, &cmd, 1);
	switch (cmd) {
	case PAUSE:
		if (!*stopped) {
			if (*paused)
				kill(chld, SIGCONT);
			else
				kill(chld, SIGSTOP);
			*paused = !*paused;
		}
		break;
	case STOP:
		if (!*stopped) {
			kill_chld(*paused);
			*stopped = 1;
			*paused = 0;
			*mod = 0;
		}
		break;
	case PREV:
		if (!*stopped) {
			kill_chld(*paused);
			*paused = 0;
			*mod = 0;
		}
		*cur_song = (*cur_song)->prev;
		if (!*cur_song)
			*cur_song = *playlist = rand_playlist(*playlist);
		break;
	case NEXT:
		if (!*stopped) {
			kill_chld(*paused);
			*paused = 0;
			*mod = 0;
		}
		*cur_song = (*cur_song)->next;
		if (!cur_song)
			*cur_song = *playlist = rand_playlist(*playlist);
		break;
	case PLAY:
		if (!*stopped) {
			kill_chld(*paused);
		}
		*paused = 0;
		*mod = 0;
		*stopped = 0;
		break;
	case JUMP:
		if (read(fd, &len, sizeof (len)) != sizeof (len))
			break;
		if (len > 256)
			break;
		title = malloc(len + 1);
		len = read(fd, title, len);
		if (len < 0) {
			free(title);
			break;
		}
		title[len] = 0;
		tmp_list = find_song(*playlist, *cur_song, title);
		if (tmp_list) {
			*cur_song = tmp_list;
			if (!*stopped) {
				kill_chld(*paused);
				paused = 0;
				mod = 0;
			}
		}
		free(title);
		break;
	case TITLE:
		title = strdup(strrchr((*cur_song)->data, '/'));
		title[strlen(title) - 4] = '\n';
		title[strlen(title) - 3] = 0;
		if (*stopped)
			title[0] = 0;
		else
			title[0] = 1;
		write(fd, title, strlen(title + 1) + 1);
		free(title);
		break;
	case ADD:
		read(fd, &len, sizeof (len));
		if (len > 256)
			break;
		title = malloc(len + 1);
		len = read(fd, title, len);
		if (len < 0) {
			free(title);
			break;
		}
		title[len] = 0;
		srand(time(NULL));
		*playlist = list_insert(*playlist, title,
							   rand() % list_length(*playlist));
		break;
	case LIST:
		tmp_list = *cur_song;
		len = 5;
		while (tmp_list->next && len < 11) {
			tmp_list = tmp_list->next;
			len++;
		}
		while (tmp_list->prev && len) {
			tmp_list = tmp_list->prev;
			len--;
		}
		cmd = 1;
		write(fd, &cmd, 1);
		pos = list_length(*playlist) - list_length(tmp_list);
		len = 0;
		while (tmp_list && len < 11) {
			char m[10];
			title = strrchr(tmp_list->data, '/');
			title++;
			snprintf(m, 10, "%d. ", pos + len);
			write(fd, m, strlen(m));
			write(fd, title, strlen(title) - 4);
			write(fd, "\n", 1);
			len++;
			tmp_list = tmp_list->next;
		}
		break;
	case EXIT:
		if (!*stopped) {
			kill_chld(*paused);
		}
		return (1);
	}
	close(fd);
	return (0);
}

int
main(int argc, char **argv)
{
	list *playlist;
	int sock;
#ifdef ICES2
	pid_t ices;
	int pfd[2];
#endif

	signal(SIGINT, sigint);
	signal(SIGTERM, sigint);
	signal(SIGCHLD, SIG_IGN);

	if (argc < 2) {
		if ((sock = create_socket()) < 0)
			return (1);
	} else {
		process(argc, argv);
		return (0);
	}

	if (!(playlist = read_playlist(NULL, PATH)))
		return (2);

	if (fork())
		return (0);

#ifdef ICES2
	fclose(stderr);

	if (pipe(pfd) == -1)
		return (0);

	ices = fork();
	if (ices < 0)
		return (0);
	if (ices == 0) {
		char *args[8];
		int a = 0;
		close(pfd[1]);
		dup2(pfd[0], fileno(stdin));
		fclose(stdout);
		args[a++] = "ices2";
		args[a++] = ICES_CONF;
		args[a++] = NULL;
		execvp(args[0], args);
		_exit(0);
	} else {
		close(pfd[0]);
		fclose(stdout);
		fclose(stdin);
	}
#else
	fclose(stdin);
	fclose(stderr);
#endif

	while (1) {
		int stopped = 0;
		list *cur_song;
		playlist = rand_playlist(playlist);
		cur_song = playlist;

		while (cur_song) {
			int paused = 0;
			int mod;

			while (stopped) {
				if (process_cmd(sock, &cur_song, &playlist,
								&stopped, &paused, &mod))
					return (0);
			}

			if (!(chld = fork())) {
				char *args[8];
				char *x = strrchr(cur_song->data, '.');
				int a = 0;
				if (!x)
					_exit(0);
				if (!strcasecmp(x, ".ogg")) {
					args[a++] = "/usr/bin/ogg123";
					args[a++] = "-d";
					args[a++] = "raw";
					args[a++] = "-f";
					args[a++] = "/dev/stdout";
				} else {
					args[a++] = "/usr/bin/mpg123";
					args[a++] = "-s";
				}
				args[a++] = "-q";
				args[a++] = cur_song->data;
				args[a++] = NULL;
				execv(args[0], args);
				_exit(0);
			}

			mod = 1;

			while (!waitpid(chld, NULL, WNOHANG)) {
				if (process_cmd(sock, &cur_song, &playlist,
								&stopped, &paused, &mod))
					return (0);
			}

			chld = -1;

			if (mod)
				cur_song = cur_song->next;
		}
	}
}

/* vim:set sw=4 ts=4 noet cin ai tw=80: */
