/* Copyright 2015 Thiébaud Weksteen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "ctl.h"

char *get_path(int fd, unsigned int replica)
{
	int res;
	size_t plen;
	char code;
	char *path;

	code = GET_REPLICA_PATH;
	res = write(fd, &code, 1);
	if (res != 1) {
		fprintf(stderr, "Unable to send command\n");
		exit(EXIT_FAILURE);
	}
	res = write(fd, &replica, sizeof(unsigned int));
	if (res != sizeof(unsigned int)) {
		fprintf(stderr, "Unable to send replica ID\n");
		exit(EXIT_FAILURE);
	}
	res = read(fd, &plen, sizeof(size_t));
	if (res != sizeof(size_t)) {
		fprintf(stderr, "Unable to read path length\n");
		exit(EXIT_FAILURE);
	}
	path = malloc(plen * (sizeof(char) + 1));
	res = read(fd, path, plen);
	if (res != plen) {
		fprintf(stderr, "Unable to read path\n");
		exit(EXIT_FAILURE);
	}
	return path;
}

int get_priority(int fd, unsigned int replica)
{
	int res;
	char code;
	int priority;

	code = GET_REPLICA_PRIORITY;
	res = write(fd, &code, 1);
	if (res != 1) {
		fprintf(stderr, "Unable to send command\n");
		exit(EXIT_FAILURE);
	}
	res = write(fd, &replica, sizeof(unsigned int));
	if (res != sizeof(unsigned int)) {
		fprintf(stderr, "Unable to send replica ID\n");
		exit(EXIT_FAILURE);
	}
	res = read(fd, &priority, sizeof(int));
	if (res != sizeof(int)) {
		fprintf(stderr, "Unable to receive replica priority\n");
		exit(EXIT_FAILURE);
	}
	return priority;
}

int get_status(int fd, unsigned int replica)
{
	int res;
	char code;
	int status;

	code = GET_REPLICA_STATUS;
	res = write(fd, &code, 1);
	if (res != 1) {
		fprintf(stderr, "Unable to send command\n");
		exit(EXIT_FAILURE);
	}
	res = write(fd, &replica, sizeof(unsigned int));
	if (res != sizeof(unsigned int)) {
		fprintf(stderr, "Unable to send replica ID\n");
		exit(EXIT_FAILURE);
	}
	res = read(fd, &status, sizeof(int));
	if (res != sizeof(int)) {
		fprintf(stderr, "Unable to receive replica status\n");
		exit(EXIT_FAILURE);
	}
	return status;
}

unsigned int get_max_replica(int fd)
{
	int res;
	unsigned int max_replica;
	char code;

	code = GET_MAX_REPLICA;
	res = write(fd, &code, 1);
	if (res != 1) {
		fprintf(stderr, "Unable to send command\n");
		exit(EXIT_FAILURE);
	}
	res = read(fd, &max_replica, sizeof(unsigned int));
	if (res != sizeof(unsigned int)) {
		fprintf(stderr, "Bad response\n");
		exit(EXIT_FAILURE);
	}
	return max_replica;
}

int main(int argc, char *argv[])
{
	unsigned int i, max_replica;
	int sfd, res = 0;
	char *path;
	struct sockaddr_un addr;

	sfd = socket(AF_LOCAL, SOCK_STREAM, 0);
	if (sfd == -1) {
		perror("socket creation failed");
		exit(EXIT_FAILURE);
	}

	memset(&addr, 0, sizeof(struct sockaddr_un));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, argv[1], sizeof(addr.sun_path) - 1);

	if(connect(sfd, (struct sockaddr *) &addr,
		   sizeof(struct sockaddr_un)) == -1) {
		perror("Unable to connect to the control socket");
		exit(EXIT_FAILURE);
	}

	max_replica = get_max_replica(sfd);
	printf("Number of replicas: %x\n", max_replica);
	printf("Replicas:\n");
	for(i = 0; i < max_replica; i++) {
		path = get_path(sfd, i);
		printf("\t[%u] %s\n", i, path);
		printf("\t\tstatus: %s\n", get_status(sfd, i) ? "disabled" : "enabled");
		printf("\t\tpriority: %s\n", get_priority(sfd, i) ?
		       "low" : "high");
		free(path);
	}

	return res;
}
