#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "edge_config.h"
#include "edge_modbus.h"

static const char test_modbus_host[] = "127.0.0.1";
static const uint16_t test_modbus_port = 1503u;
static const char test_modbus_port_text[] = "1503";

static char *unconst(const char *s) {
    return (char *)(uintptr_t)s;
}

static EdgeModel g_model;

static int create_extended_template(const char *source_path, char *path, size_t path_size) {
    char temp_path[] = "/tmp/opcua-edge-modbus-XXXXXX.edge";
    int fd;
    FILE *src;
    FILE *dst;
    char buffer[4096];
    size_t nread;

    if(strlen(temp_path) + 1u > path_size) {
        return 1;
    }

    fd = mkstemps(temp_path, 5);
    if(fd < 0) {
        return 1;
    }
    dst = fdopen(fd, "w");
    if(dst == 0) {
        close(fd);
        unlink(temp_path);
        return 1;
    }
    src = fopen(source_path, "r");
    if(src == 0) {
        fclose(dst);
        unlink(temp_path);
        return 1;
    }

    while((nread = fread(buffer, 1, sizeof(buffer), src)) > 0u) {
        if(fwrite(buffer, 1, nread, dst) != nread) {
            fclose(src);
            fclose(dst);
            unlink(temp_path);
            return 1;
        }
    }
    if(ferror(src) != 0) {
        fclose(src);
        fclose(dst);
        unlink(temp_path);
        return 1;
    }

    if(fprintf(dst,
        "\n[node]\n"
        "asset=hp_pump_1\n"
        "id=test_signed_32\n"
        "name=TestSigned32\n"
        "data_type=int32\n"
        "access=read_write\n"
        "source=modbus\n"
        "modbus_unit=3\n"
        "modbus_register=40120\n"
        "scale=1.0\n"
        "\n[node]\n"
        "asset=hp_pump_1\n"
        "id=test_unsigned_32\n"
        "name=TestUnsigned32\n"
        "data_type=uint32\n"
        "access=read_write\n"
        "source=modbus\n"
        "modbus_unit=3\n"
        "modbus_register=40122\n"
        "scale=1.0\n") < 0) {
        fclose(src);
        fclose(dst);
        unlink(temp_path);
        return 1;
    }

    fclose(src);
    if(fclose(dst) != 0) {
        unlink(temp_path);
        return 1;
    }
    memcpy(path, temp_path, strlen(temp_path) + 1u);
    return 0;
}

static pid_t spawn_simulator(const char *script_path, const char *template_path) {
    pid_t pid = fork();
    if(pid < 0) {
        return -1;
    }
    if(pid == 0) {
        char *args[10];
        int n = 0;
        args[n++] = unconst("python3");
        args[n++] = unconst(script_path);
        args[n++] = unconst("--template");
        args[n++] = unconst(template_path);
        args[n++] = unconst("--port");
        args[n++] = unconst(test_modbus_port_text);
        args[n++] = unconst("--profile");
        args[n++] = unconst("high_vibration");
        args[n++] = unconst("--quiet");
        args[n] = 0;
        execvp("python3", args);
        _exit(127);
    }
    return pid;
}

static int wait_for_simulator(EdgeModbus *modbus) {
    struct timespec delay = {0, 100 * 1000 * 1000};
    int attempt;
    for(attempt = 0; attempt < 50; attempt++) {
        if(edge_modbus_connect(modbus) == EDGE_STATUS_OK && modbus->connected != 0) {
            return 0;
        }
        nanosleep(&delay, 0);
    }
    return 1;
}

static int stop_simulator(pid_t pid) {
    struct timespec poll_delay = {0, 100 * 1000 * 1000};
    int status = 0;
    int attempt;
    if(pid <= 0) {
        return 0;
    }
    kill(pid, SIGTERM);
    for(attempt = 0; attempt < 30; attempt++) {
        pid_t reaped = waitpid(pid, &status, WNOHANG);
        if(reaped == pid) {
            return 0;
        }
        if(reaped < 0) {
            return 1;
        }
        nanosleep(&poll_delay, 0);
    }
    kill(pid, SIGKILL);
    if(waitpid(pid, &status, 0) < 0) {
        return 1;
    }
    return 0;
}

static EdgeNode *find_node(const char *asset_id, const char *node_id) {
    uint32_t i;
    for(i = 0; i < g_model.node_count; i++) {
        if(strcmp(g_model.nodes[i].asset_id, asset_id) == 0 &&
           strcmp(g_model.nodes[i].id, node_id) == 0) {
            return &g_model.nodes[i];
        }
    }
    return 0;
}

static int verify_high_vibration_profile(void) {
    EdgeNode *node = find_node("hp_pump_1", "vibration");
    double diff;
    if(node == 0) {
        fprintf(stderr, "hp_pump_1.vibration not found\n");
        return 1;
    }
    diff = node->value.d - 9.2;
    if(diff < 0.0) diff = -diff;
    if(diff > 0.001) {
        fprintf(stderr, "hp_pump_1.vibration expected 9.2, got %g\n", node->value.d);
        return 1;
    }
    return 0;
}

static int verify_command_round_trip(EdgeModbus *modbus) {
    EdgeNode *cmd = find_node("hp_pump_1", "command_start");
    if(cmd == 0) {
        fprintf(stderr, "hp_pump_1.command_start not found\n");
        return 1;
    }

    cmd->value.b = 1;
    if(edge_modbus_write_node(modbus, cmd) != EDGE_STATUS_OK) {
        fprintf(stderr, "write 1 failed\n");
        return 1;
    }
    if(edge_modbus_poll(modbus, &g_model) != EDGE_STATUS_OK) {
        fprintf(stderr, "poll after write 1 failed\n");
        return 1;
    }
    if(cmd->value.b != 1u) {
        fprintf(stderr, "command_start did not read back as 1: b=%u\n", cmd->value.b);
        return 1;
    }

    cmd->value.b = 0;
    if(edge_modbus_write_node(modbus, cmd) != EDGE_STATUS_OK) {
        fprintf(stderr, "write 0 failed\n");
        return 1;
    }
    if(edge_modbus_poll(modbus, &g_model) != EDGE_STATUS_OK) {
        fprintf(stderr, "poll after write 0 failed\n");
        return 1;
    }
    if(cmd->value.b != 0u) {
        fprintf(stderr, "command_start did not read back as 0\n");
        return 1;
    }

    return 0;
}

static int verify_32bit_round_trip(EdgeModbus *modbus) {
    EdgeNode *signed_node = find_node("hp_pump_1", "test_signed_32");
    EdgeNode *unsigned_node = find_node("hp_pump_1", "test_unsigned_32");
    if(signed_node == 0 || unsigned_node == 0) {
        fprintf(stderr, "32-bit test nodes not found\n");
        return 1;
    }

    signed_node->value.i32 = -123456789;
    if(edge_modbus_write_node(modbus, signed_node) != EDGE_STATUS_OK) {
        fprintf(stderr, "signed 32-bit write failed\n");
        return 1;
    }
    unsigned_node->value.u32 = 0xfedc1234u;
    if(edge_modbus_write_node(modbus, unsigned_node) != EDGE_STATUS_OK) {
        fprintf(stderr, "unsigned 32-bit write failed\n");
        return 1;
    }
    if(edge_modbus_poll(modbus, &g_model) != EDGE_STATUS_OK) {
        fprintf(stderr, "poll after 32-bit writes failed\n");
        return 1;
    }
    if(signed_node->value.i32 != -123456789) {
        fprintf(stderr, "signed 32-bit round trip mismatch: %d\n", signed_node->value.i32);
        return 1;
    }
    if(unsigned_node->value.u32 != 0xfedc1234u) {
        fprintf(stderr, "unsigned 32-bit round trip mismatch: %u\n", unsigned_node->value.u32);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    EdgeModbus modbus;
    pid_t sim_pid;
    int rc = 1;
    char template_path[256];

    if(argc != 3) {
        fprintf(stderr, "usage: test_modbus <template-path> <simulator-script>\n");
        return 1;
    }

    memset(&g_model, 0, sizeof(g_model));
    memset(&modbus, 0, sizeof(modbus));

    if(create_extended_template(argv[1], template_path, sizeof(template_path)) != 0) {
        fprintf(stderr, "failed to create extended template\n");
        return 1;
    }

    if(edge_config_load(template_path, &g_model) != EDGE_STATUS_OK ||
       edge_config_wire_runtime(&g_model) != EDGE_STATUS_OK) {
        fprintf(stderr, "failed to load model\n");
        unlink(template_path);
        return 1;
    }

    sim_pid = spawn_simulator(argv[2], template_path);
    if(sim_pid < 0) {
        fprintf(stderr, "failed to spawn simulator\n");
        return 1;
    }

    if(edge_modbus_init(&modbus, test_modbus_host, test_modbus_port) != EDGE_STATUS_OK) {
        fprintf(stderr, "modbus init failed\n");
        goto cleanup;
    }

    if(wait_for_simulator(&modbus) != 0) {
        fprintf(stderr, "simulator did not become reachable\n");
        goto cleanup;
    }

    if(edge_modbus_poll(&modbus, &g_model) != EDGE_STATUS_OK) {
        fprintf(stderr, "initial poll failed\n");
        goto cleanup;
    }

    if(modbus.read_ok_count != g_model.node_count) {
        fprintf(stderr, "expected %u reads, got %llu (errors=%llu)\n",
                g_model.node_count,
                (unsigned long long)modbus.read_ok_count,
                (unsigned long long)modbus.read_error_count);
        goto cleanup;
    }

    if(verify_high_vibration_profile() != 0) {
        goto cleanup;
    }

    if(verify_command_round_trip(&modbus) != 0) {
        goto cleanup;
    }

    if(verify_32bit_round_trip(&modbus) != 0) {
        goto cleanup;
    }

    rc = 0;

cleanup:
    edge_modbus_clear(&modbus);
    stop_simulator(sim_pid);
    unlink(template_path);
    return rc;
}
