#!/usr/bin/env python3
"""Manage this project's local OJ processes using only the Python standard library."""

import argparse
from contextlib import contextmanager
import fcntl
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import sys
import time
import urllib.request


ROOT = Path(__file__).resolve().parent.parent
OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def positive_port(value):
    try:
        port = int(value)
    except ValueError:
        raise argparse.ArgumentTypeError("端口必须是整数")
    if not 1 <= port <= 65535:
        raise argparse.ArgumentTypeError("端口必须在 1..65535 之间")
    return port


def port_list(value):
    ports = [positive_port(item) for item in value.split(",")]
    if len(set(ports)) != len(ports):
        raise argparse.ArgumentTypeError("判题节点端口不能重复")
    return ports


def read_state(directory):
    path = directory / "state.json"
    if not path.exists():
        return None
    try:
        state = json.loads(path.read_text())
        ports = state["compile_ports"]
        if (state["version"] != 1 or type(state["port"]) is not int
                or not 1 <= state["port"] <= 65535
                or not isinstance(ports, list) or not ports
                or not all(type(port) is int and 1 <= port <= 65535 for port in ports)
                or len(set(ports + [state["port"]])) != len(ports) + 1
                or not isinstance(state["services"], dict)):
            raise ValueError("invalid configuration")
        names = {"oj"} | {"compile-" + str(port) for port in ports}
        for name, record in state["services"].items():
            if (name not in names or type(record["pid"]) is not int or record["pid"] <= 1
                    or not isinstance(record["start_time"], str) or not record["start_time"].isdigit()):
                raise ValueError("invalid process identity")
        return state
    except (ValueError, KeyError, TypeError) as error:
        raise RuntimeError("运行记录格式错误，请检查 " + str(path)) from error


def write_state(directory, state):
    temporary = directory / "state.json.tmp"
    temporary.write_text(json.dumps(state, indent=2) + "\n")
    temporary.replace(directory / "state.json")


@contextmanager
def locked(directory):
    directory.mkdir(parents=True, exist_ok=True)
    with (directory / "manager.lock").open("a") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise RuntimeError("另一个管理命令正在运行，请稍后再试") from error
        yield


def specifications(directory, state):
    services = []
    for port in state["compile_ports"]:
        name = "compile-" + str(port)
        binary = ROOT / "compile_server" / "compile_server"
        services.append(dict(name=name, port=port, binary=binary,
                             cwd=directory / "nodes" / name,
                             argv=[str(binary), str(port)], service="compile_server"))
    binary = ROOT / "oj_server" / "oj_server"
    services.append(dict(name="oj", port=state["port"], binary=binary,
                         cwd=ROOT / "oj_server", service="oj_server",
                         argv=[str(binary), "--nodes", str(directory / "compile_nodes.conf"),
                               str(state["port"])]))
    return services


def process_identity(pid):
    try:
        # Fields after the last ')' start with state (field 3); starttime is field 22.
        fields = Path("/proc", str(pid), "stat").read_text().rsplit(")", 1)[1].split()
        if fields[0] in ("Z", "X"):
            return None
        executable = os.readlink("/proc/%d/exe" % pid)
        if executable.endswith(" (deleted)"):
            executable = executable[:-10]
        return fields[19], executable
    except (OSError, IndexError):
        return None


def matches(spec, record):
    return bool(record) and process_identity(record["pid"]) == (
        record["start_time"], str(spec["binary"]))


def healthy(spec):
    try:
        with OPENER.open("http://127.0.0.1:%d/health" % spec["port"], timeout=1) as response:
            data = json.load(response)
            return (response.status == 200 and isinstance(data, dict)
                    and data.get("ok") is True and data.get("service") == spec["service"])
    except (OSError, ValueError):
        return False


def stop_process(spec, record):
    if not matches(spec, record):
        return
    pid = record["pid"]
    # Every managed process starts a new session; include compiler/runner children.
    try:
        if os.getpgid(pid) != pid:
            raise RuntimeError(spec["name"] + " 的进程组与记录不符，未发送信号")
        os.killpg(pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    deadline = time.monotonic() + 3
    while matches(spec, record) and time.monotonic() < deadline:
        time.sleep(0.05)
    if matches(spec, record):
        try:
            os.killpg(pid, signal.SIGKILL)
        except ProcessLookupError:
            return
        deadline = time.monotonic() + 2
        while matches(spec, record) and time.monotonic() < deadline:
            time.sleep(0.05)
    if matches(spec, record):
        raise RuntimeError(spec["name"] + " 停止超时，运行记录已保留")


def start(directory, args):
    state = read_state(directory)
    if state is None:
        state = dict(version=1, port=args.port or 8080,
                     compile_ports=args.compile_ports or ([8081, 8082, 8083] if args.multi else [8081]),
                     services={})
    elif ((args.port is not None and args.port != state["port"])
          or (args.compile_ports is not None and args.compile_ports != state["compile_ports"])
          or (args.multi and args.compile_ports is None and state["compile_ports"] != [8081, 8082, 8083])):
        raise RuntimeError("运行配置不同，请先执行 stop，再使用新参数 start")
    if state["port"] in state["compile_ports"]:
        raise RuntimeError("OJ 页面端口不能与判题节点端口相同")

    specs = specifications(directory, state)
    missing = []
    for spec in specs:
        record = state["services"].get(spec["name"])
        if matches(spec, record):
            if not healthy(spec):
                raise RuntimeError(spec["name"] + " 进程存在但健康检查失败，请先 stop " + spec["name"])
        else:
            missing.append(spec)

    # Check every missing port before launching anything; never adopt another service.
    for spec in missing:
        with socket.socket() as probe:
            probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            try:
                probe.bind(("0.0.0.0", spec["port"]))
            except OSError as error:
                raise RuntimeError("端口 %d 无法使用：%s；未停止或接管占用者" % (spec["port"], error)) from error
    if not missing:
        print("服务已在运行，本次没有重复启动。")
        return status(directory, state)

    for subdirectory in ("compile_server", "oj_server"):
        subprocess.run(["make", "-C", str(ROOT / subdirectory), subdirectory], check=True)
    (directory / "logs").mkdir(exist_ok=True)
    (directory / "compile_nodes.conf").write_text(
        "".join("127.0.0.1 %d\n" % port for port in state["compile_ports"]))
    started = []
    try:
        for spec in missing:
            spec["cwd"].mkdir(parents=True, exist_ok=True)
            log_path = directory / "logs" / (spec["name"] + ".log")
            with log_path.open("ab", buffering=0) as log:
                process = subprocess.Popen(spec["argv"], cwd=spec["cwd"], stdin=subprocess.DEVNULL,
                                           stdout=log, stderr=subprocess.STDOUT,
                                           start_new_session=True, close_fds=True)
            # Popen has completed exec before returning; capture Linux's PID identity.
            identity = process_identity(process.pid)
            if identity is None or identity[1] != str(spec["binary"]):
                process.wait(timeout=2)
                raise RuntimeError(spec["name"] + " 启动失败，请查看 " + str(log_path))
            record = dict(pid=process.pid, start_time=identity[0])
            started.append((spec, record, process))
            state["services"][spec["name"]] = record
            write_state(directory, state)
            deadline = time.monotonic() + 8
            while not healthy(spec):
                if process.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError(spec["name"] + " 启动失败，请查看 " + str(log_path))
                time.sleep(0.1)
            if process.poll() is not None or not matches(spec, record):
                raise RuntimeError(spec["name"] + " 启动后退出，请查看 " + str(log_path))
            print("已启动 %s，PID %d，端口 %d" % (spec["name"], process.pid, spec["port"]))
    except BaseException:
        for spec, record, process in reversed(started):
            stop_process(spec, record)
            process.wait(timeout=2)
            state["services"].pop(spec["name"], None)
        write_state(directory, state)
        raise
    return status(directory, state)


def status(directory, state=None):
    state = state if state is not None else read_state(directory)
    if state is None:
        print("当前没有脚本管理的服务。运行目录：" + str(directory))
        return 1
    success = True
    print("服务             端口    PID       状态")
    for spec in specifications(directory, state):
        record = state["services"].get(spec["name"])
        alive = matches(spec, record)
        ok = alive and healthy(spec)
        label = "运行正常" if ok else ("健康检查失败" if alive else "已停止 / 记录失效")
        print("%-16s %-7d %-9s %s" % (spec["name"], spec["port"], str(record["pid"]) if alive else "-", label))
        success = success and ok
    print("页面：http://127.0.0.1:%d/" % state["port"])
    print("日志：" + str(directory / "logs"))
    return 0 if success else 1


def stop(directory, name):
    state = read_state(directory)
    if state is None:
        print("当前没有脚本管理的服务。")
        return 0
    specs = specifications(directory, state)
    if name and name not in {spec["name"] for spec in specs}:
        raise RuntimeError("未知服务名，可先执行 status 查看")
    for spec in reversed(specs):
        if name and name != spec["name"]:
            continue
        record = state["services"].get(spec["name"])
        alive = matches(spec, record)
        stop_process(spec, record)
        state["services"].pop(spec["name"], None)
        write_state(directory, state)
        print(spec["name"] + (" 已停止" if alive else " 已退出或记录失效，未发送信号"))
    if name is None:
        (directory / "state.json").unlink()
    print("日志与任务文件保留在：" + str(directory))
    return 0


def logs(directory, args):
    paths = sorted((directory / "logs").glob("*.log"))
    if args.name:
        paths = [path for path in paths if path.stem == args.name]
    if not paths:
        raise RuntimeError("没有找到对应日志：" + str(directory / "logs"))
    command = ["tail", "-n", str(args.lines)]
    if args.follow:
        command.append("-f")
    return subprocess.run(command + [str(path) for path in paths]).returncode


def main():
    parser = argparse.ArgumentParser(description="OJ 本地启动、停止、状态与日志管理")
    parser.add_argument("--run-dir", type=Path, default=ROOT / "run", help="运行目录，默认项目内 run/")
    commands = parser.add_subparsers(dest="command", required=True)
    start_parser = commands.add_parser("start", help="编译并启动；已有记录时补齐已停止节点")
    start_parser.add_argument("--multi", action="store_true", help="首次启动默认使用 8081/8082/8083 三个节点")
    start_parser.add_argument("--port", type=positive_port, help="页面端口，首次启动默认 8080")
    start_parser.add_argument("--compile-ports", type=port_list, help="本机判题端口，如 18081,18082")
    commands.add_parser("status", help="查看进程身份及各自 /health；不代表 OJ 内部节点已恢复")
    stop_parser = commands.add_parser("stop", help="停止全部服务或指定节点")
    stop_parser.add_argument("name", nargs="?", help="oj 或 compile-8081 等服务名")
    log_parser = commands.add_parser("logs", help="查看日志，Ctrl+C 结束跟随")
    log_parser.add_argument("name", nargs="?")
    log_parser.add_argument("--lines", type=int, default=40)
    log_parser.add_argument("--follow", "-f", action="store_true")
    args = parser.parse_args()
    directory = args.run_dir.expanduser().resolve()
    if args.command == "logs":
        if args.lines < 0:
            parser.error("--lines 不能为负数")
        return logs(directory, args)
    with locked(directory):
        if args.command == "start":
            return start(directory, args)
        if args.command == "stop":
            return stop(directory, args.name)
        return status(directory)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print("错误：" + str(error), file=sys.stderr)
        sys.exit(1)
    except KeyboardInterrupt:
        sys.exit(130)
