/*
 * lxr_sweep.c
 *
 * The LXR's golden renders and health checks (PLAN.md, "Testing"): every
 * sound parameter at its minimum, middle and maximum, each case 2 s of the
 * demo groove from a freshly booted machine, plus the groove with the kit
 * untouched ("base"). All of it twice: on the init kit ("init") and on a busy
 * kit with FX sends, LFOs and velocity modulation on ("busy", see
 * web_test_busy_kit), so parameters the init kit leaves idle are heard too.
 * One line per case:
 *
 *   <kit> <param> <value> <hash> peak=<abs max> dc=<mean> clip=<full-scale samples> bad=<NaN/Inf samples>
 *
 * The firmware boots once; each case runs in a forked child, so every case
 * starts from the same state and the cases run in parallel. Exit code 1 if
 * any render contains NaN or Inf.
 *
 *   lxr_sweep [--seconds S] [--rate HZ] [--jobs N] [--only PARAM]
 *
 * Copyright (C) 2026 Erica Synths
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

void web_log(const char *msg) { (void)msg; }
void web_sd_written(uint32_t firstSector, uint32_t count) { (void)firstSector; (void)count; }
void web_midi_out(uint8_t byte) { (void)byte; }

uint8_t *web_sd_alloc(uint32_t sectors);
void web_boot(float rate, uint32_t seed);
float *web_render(void);
int web_block_size(void);
void web_test_demo_block(uint32_t block, float rate);
void web_test_set_param(uint16_t param, uint8_t value);
uint16_t web_test_num_sound_params(void);
uint16_t web_test_param_range(uint16_t param);
void web_test_busy_kit(void);
void web_test_load_kit(void);

typedef struct { int busy, param, value; } Case;  // param -1: the kit untouched

static void runCase(Case c, float seconds, float rate, int fd)
{
	if (c.busy)
		web_test_busy_kit();
	if (c.param >= 0)
		web_test_set_param((uint16_t)c.param, (uint8_t)c.value);
	web_test_load_kit();
	const int n = web_block_size();
	const uint32_t blocks = (uint32_t)(seconds * rate / n);
	uint64_t hash = 1469598103934665603ull;
	double sum = 0, peak = 0;
	long clip = 0, bad = 0, count = 0;
	for (uint32_t b = 0; b < blocks; b++)
	{
		web_test_demo_block(b, rate);
		const float *out = web_render();
		const uint8_t *bytes = (const uint8_t *)out;
		for (size_t i = 0; i < sizeof(float) * 4 * (size_t)n; i++)
			hash = (hash ^ bytes[i]) * 1099511628211ull;
		for (int i = 0; i < 4 * n; i++)
		{
			const float v = out[i];
			if (!isfinite(v)) { bad++; continue; }
			const double a = fabs(v);
			if (a > peak) peak = a;
			if (a >= 32767.f / 32768.f) clip++;
			sum += v;
			count++;
		}
	}
	char line[160];
	char name[16];
	if (c.param < 0) snprintf(name, sizeof name, "base");
	else snprintf(name, sizeof name, "%d", c.param);
	const int len = snprintf(line, sizeof line, "%s %-4s %3d %016llx peak=%.4f dc=%+.5f clip=%ld bad=%ld\n",
	                         c.busy ? "busy" : "init", name, c.value, (unsigned long long)hash, peak, count ? sum / count : 0.0, clip, bad);
	if (write(fd, line, (size_t)len) != len)
		_exit(3);
}

int main(int argc, char **argv)
{
	float seconds = 2.f, rate = 44100.f;
	int jobs = (int)sysconf(_SC_NPROCESSORS_ONLN), only = -2;
	for (int i = 1; i + 1 < argc; i += 2)
	{
		if (!strcmp(argv[i], "--seconds")) seconds = (float)atof(argv[i + 1]);
		else if (!strcmp(argv[i], "--rate")) rate = (float)atof(argv[i + 1]);
		else if (!strcmp(argv[i], "--jobs")) jobs = atoi(argv[i + 1]);
		else if (!strcmp(argv[i], "--only")) only = atoi(argv[i + 1]);
		else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
	}
	if (jobs < 1) jobs = 1;

	web_sd_alloc(32768);
	web_boot(rate, 1);

	// the cases: the base, then min, middle and max of every sound parameter
	const int np = web_test_num_sound_params();
	Case *cases = malloc(sizeof(Case) * 2 * (1 + 3 * np));
	int nc = 0;
	for (int busy = 0; busy < 2; busy++)
	{
		if (only < -1) cases[nc++] = (Case){busy, -1, 0};
		for (int p = 0; p < np; p++)
		{
			if (only >= -1 && p != only) continue;
			const uint16_t r = web_test_param_range((uint16_t)p);
			const int lo = r & 0xFF, hi = r >> 8, mid = (lo + hi) / 2;
			cases[nc++] = (Case){busy, p, lo};
			if (mid != lo && mid != hi) cases[nc++] = (Case){busy, p, mid};
			if (hi != lo) cases[nc++] = (Case){busy, p, hi};
		}
	}

	// run them, at most `jobs` at a time, and print the results in case order
	char **results = calloc((size_t)nc, sizeof(char *));
	pid_t *pids = calloc((size_t)nc, sizeof(pid_t));
	int *fds = calloc((size_t)nc, sizeof(int));
	int next = 0, done = 0, running = 0, failed = 0;
	while (done < nc)
	{
		while (running < jobs && next < nc)
		{
			int fd[2];
			if (pipe(fd)) { perror("pipe"); return 2; }
			const pid_t pid = fork();
			if (pid == 0)
			{
				close(fd[0]);
				runCase(cases[next], seconds, rate, fd[1]);
				_exit(0);
			}
			close(fd[1]);
			pids[next] = pid;
			fds[next] = fd[0];
			next++;
			running++;
		}
		// collect the oldest running case
		for (int i = done; i < next; i++)
		{
			if (!pids[i]) continue;
			char buf[200] = {0};
			ssize_t got = 0, r;
			while ((r = read(fds[i], buf + got, sizeof buf - 1 - (size_t)got)) > 0) got += r;
			close(fds[i]);
			int status = 0;
			waitpid(pids[i], &status, 0);
			pids[i] = 0;
			running--;
			if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 || got == 0)
			{
				snprintf(buf, sizeof buf, "%s %-4d %3d CRASHED (status %d)\n", cases[i].busy ? "busy" : "init", cases[i].param, cases[i].value, status);
				failed = 1;
			}
			else if (!strstr(buf, "bad=0\n"))
				failed = 1;
			results[i] = strdup(buf);
			break;
		}
		while (done < next && results[done])
			fputs(results[done++], stdout);
	}
	return failed;
}
