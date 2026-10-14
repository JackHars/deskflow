// SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
// SPDX-License-Identifier: MIT

// Disposable GNOME integration fixture. The only input endpoint is the private
// descriptor received from the provider on the harness's private session bus.
// This is not an input backend or a production delivery-authority provider.
#define _POSIX_C_SOURCE 200809L
#include <libei.h>

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static uint64_t now_ms(void)
{
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    abort();
  return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

int main(int argc, char **argv)
{
  if (argc != 2)
    return 2;
  char *end = NULL;
  long fd = strtol(argv[1], &end, 10);
  if (!end || *end || fd < 3 || fd > 1048576)
    return 2;
  setvbuf(stdout, NULL, _IOLBF, 0);
  struct ei *ei = ei_new_sender(NULL);
  if (!ei)
    return 3;
  ei_configure_name(ei, "Deskflow private GNOME provider fixture");
  if (ei_setup_backend_fd(ei, (int)fd) != 0) {
    ei_unref(ei);
    return 3;
  }
  struct ei_device *pointer = NULL;
  struct ei_device *keyboard = NULL;
  struct ei_ping *pending = NULL;
  uint64_t request = 0;
  uint64_t last_request = 0;
  bool connected = false;
  bool pointer_ready = false;
  bool keyboard_ready = false;
  bool ready = false;
  int result = 4;
  char command[256];
  size_t used = 0;
  const uint64_t deadline = now_ms() + 120000;

  while (now_ms() < deadline) {
    bool disconnected = false;
    bool pong = false;
    ei_dispatch(ei);
    struct ei_event *event;
    while ((event = ei_get_event(ei)) != NULL) {
      struct ei_device *device = ei_event_get_device(event);
      switch (ei_event_get_type(event)) {
      case EI_EVENT_CONNECT:
        connected = true;
        break;
      case EI_EVENT_SEAT_ADDED:
        ei_seat_bind_capabilities(
            ei_event_get_seat(event), EI_DEVICE_CAP_POINTER_ABSOLUTE, EI_DEVICE_CAP_BUTTON, EI_DEVICE_CAP_KEYBOARD, NULL
        );
        break;
      case EI_EVENT_DEVICE_ADDED:
        if (!pointer && ei_device_has_capability(device, EI_DEVICE_CAP_POINTER_ABSOLUTE))
          pointer = ei_device_ref(device);
        if (!keyboard && ei_device_has_capability(device, EI_DEVICE_CAP_KEYBOARD))
          keyboard = ei_device_ref(device);
        break;
      case EI_EVENT_DEVICE_RESUMED:
        if (device == pointer || device == keyboard)
          ei_device_start_emulating(device, 1);
        if (device == pointer)
          pointer_ready = true;
        if (device == keyboard)
          keyboard_ready = true;
        break;
      case EI_EVENT_DEVICE_PAUSED:
      case EI_EVENT_DEVICE_REMOVED:
        if (device == pointer)
          pointer_ready = false;
        if (device == keyboard)
          keyboard_ready = false;
        break;
      case EI_EVENT_PONG:
        if (pending && ei_event_pong_get_ping(event) == pending)
          pong = true;
        break;
      case EI_EVENT_DISCONNECT:
        disconnected = true;
        break;
      default:
        break;
      }
      ei_event_unref(event);
    }
    // libei can synthesize PONG immediately before DISCONNECT. Drain the whole
    // batch before acknowledging a fence so that this cannot pass the test.
    if (disconnected) {
      puts("{\"event\":\"disconnected\"}");
      result = 0;
      break;
    }
    if (pong) {
      if (!pointer_ready || !keyboard_ready)
        break;
      printf("{\"event\":\"synced\",\"request\":%" PRIu64 "}\n", request);
      pending = ei_ping_unref(pending);
    }
    if (!ready && connected && pointer_ready && keyboard_ready) {
      struct ei_region *region = ei_device_get_region(pointer, 0);
      if (!region)
        break;
      printf(
          "{\"event\":\"ready\",\"region\":[%u,%u,%u,%u]}\n", ei_region_get_x(region), ei_region_get_y(region),
          ei_region_get_width(region), ei_region_get_height(region)
      );
      ready = true;
    }
    struct pollfd pollfds[] = {{ei_get_fd(ei), POLLIN, 0}, {STDIN_FILENO, POLLIN, 0}};
    int polled = poll(pollfds, 2, 100);
    if (polled < 0 && errno == EINTR)
      continue;
    if (polled < 0)
      break;
    if (!(pollfds[1].revents & (POLLIN | POLLHUP)))
      continue;
    char byte;
    ssize_t count = read(STDIN_FILENO, &byte, 1);
    if (count != 1) {
      result = count == 0 ? 0 : 4;
      break;
    }
    if (byte != '\n') {
      if (used == sizeof(command) - 1)
        break;
      command[used++] = byte;
      continue;
    }
    command[used] = 0;
    used = 0;
    if (strcmp(command, "Q") == 0) {
      result = 0;
      break;
    }
    char kind, extra;
    uint64_t next_request;
    double x = 0, y = 0;
    int fields = sscanf(command, "%c %" SCNu64 " %lf %lf %c", &kind, &next_request, &x, &y, &extra);
    if (!ready || !pointer_ready || !keyboard_ready || pending || fields < 2 || next_request <= last_request)
      break;
    if (kind == 'M' && fields == 4 && isfinite(x) && isfinite(y) && ei_device_get_region_at(pointer, x, y)) {
      ei_device_pointer_motion_absolute(pointer, x, y);
      ei_device_frame(pointer, ei_now(ei));
    } else if (kind == 'E' && fields == 2) {
      // A private synthetic Escape only dismisses this disposable Shell's
      // startup overview. No host or user's compositor is connected here.
      ei_device_keyboard_key(keyboard, 1, true);
      ei_device_frame(keyboard, ei_now(ei));
      ei_device_keyboard_key(keyboard, 1, false);
      ei_device_frame(keyboard, ei_now(ei));
    } else if ((kind == 'D' || kind == 'U') && fields == 2 && ei_device_has_capability(pointer, EI_DEVICE_CAP_BUTTON)) {
      // Owned headless fixture only: exercise the compositor's ordinary move
      // operation. This process never connects to the user's input session.
      ei_device_button_button(pointer, 272, kind == 'D');
      ei_device_frame(pointer, ei_now(ei));
    } else if (!(kind == 'S' && fields == 2)) {
      break;
    }
    request = next_request;
    last_request = request;
    pending = ei_new_ping(ei);
    if (!pending)
      break;
    ei_ping(pending);
  }
  if (pending)
    ei_ping_unref(pending);
  if (pointer)
    ei_device_unref(pointer);
  if (keyboard)
    ei_device_unref(keyboard);
  ei_unref(ei);
  if (result != 0)
    fputs("EI fixture failed or exceeded its deadline\n", stderr);
  return result;
}
