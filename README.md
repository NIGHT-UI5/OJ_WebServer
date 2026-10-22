# C++ 负载均衡在线 OJ

基于 C++11 和 Linux 的学习型在线判题系统。浏览器提供题库、代码编辑、提交和结果展示；OJ Server 读取服务端测试数据，选择 Compile Server 编译、运行和比较答案。

支持 AC、WA、CE、TLE、RE、OLE，具备 CPU、内存、实际运行时间、输出大小限制，以及多节点最少活跃请求调度、故障摘除和健康检查恢复。

## 环境与启动

需要 Linux、Bash、支持 C++11 的 g++、GNU Make、JsonCpp 开发包和 Python 3.7 以上版本。HTTP 库已包含在项目中，前端无需 Node.js 或打包工具。

Ubuntu / Debian 可安装依赖：

```bash
sudo apt-get install build-essential libjsoncpp-dev python3
```

在本目录下执行：

```bash
./scripts/oj.sh start
```

脚本会自动编译并在后台启动 OJ Server（8080）和一个判题节点（8081），然后检查健康状态。

- 题目列表：<http://127.0.0.1:8080/>
- 做题页面：<http://127.0.0.1:8080/problem.html?id=1001>

远程开发时，在 VS Code「端口」面板转发 8080，使用转发地址打开页面。页面和接口由同一个 OJ Server 提供。

```bash
./scripts/oj.sh status
./scripts/oj.sh logs oj
./scripts/oj.sh logs compile-8081 --follow
./scripts/oj.sh stop
```

Ctrl+C 结束日志跟随。重复执行 `start` 会复用已有进程，补齐已停止节点。切换配置前先执行 `stop`。日志追加写入，停止服务后保留日志和任务文件。

`make` 只编译；也可使用 `make start`、`make multi`、`make stop`、`make status` 和 `make logs`。`make clean` 只删除两个服务的编译产物。

## 多节点运行

```bash
./scripts/oj.sh stop
./scripts/oj.sh start --multi
```

OJ Server 仍使用 8080，判题节点使用 8081、8082、8083。网页提交后，使用 `./scripts/oj.sh logs oj` 查看 `forward problem ... to compile server`，观察节点选择。

```bash
./scripts/oj.sh stop compile-8082
./scripts/oj.sh start
```

停止指定节点后，首次选中它的提交可能返回 502；节点被摘除后其他节点继续服务。再次 `start` 会补齐停止的节点，等待后台健康检查后恢复调度。全部节点被判定离线时返回 503，题目浏览仍可使用。

`status` 仅检查进程和各自 `/health`，不读取 OJ 内部调度状态。活跃请求数表示 OJ 正在等待的请求数量，不等于节点 CPU 负载。

自定义本机端口和运行目录：

```bash
./scripts/oj.sh --run-dir /tmp/oj-runtime start --port 18080 --compile-ports 18081,18082
./scripts/oj.sh --run-dir /tmp/oj-runtime status
./scripts/oj.sh --run-dir /tmp/oj-runtime stop
```

同一组服务的管理命令需使用相同运行目录。脚本不会接管手动启动的进程；端口被占用时，可先停止原服务或改用其他端口。

## 项目结构

```text
common/           HTTP 库、日志和文件工具
compile_server/   编译、运行、资源限制和判题服务
oj_server/        HTTP 接口、题库、多节点调度与故障恢复
data/             题目元数据与正式判题数据
web/              题库页面、做题页面和静态资源
scripts/          启停、状态和日志管理
Makefile          项目构建与管理入口
```

启动后才会生成两个服务的可执行文件及 `run/` 运行目录。其中 `run/logs/` 保存日志，`run/nodes/compile-<port>/tmp/task_*/` 保存各次提交的源码、编译诊断和输出。

## 题库和接口

题目配置位于 `data/problems.json`。每道题需要提供唯一的正整数字符串 ID、标题、难度、描述、初始代码、公开样例和四个正整数资源限制。私有判题数据位于：

```text
data/test_cases/<id>/input.txt
data/test_cases/<id>/answer.txt
```

这些文件是正式判题数据，必须随项目保留。目前每题使用一组私有测试，按字节精确比较程序输出与标准答案，末尾换行也参与比较。修改题库后停止并重新启动服务加载。

OJ Server 提供：

- `GET /health`：服务自身健康状态。
- `GET /problems`：公开题目列表。
- `GET /problems/<id>`：公开题目详情，不返回私有测试数据。
- `POST /submit`：只接收 `{"problem_id":"1001","code":"完整 C++ 源码"}`。

测试输入、标准答案和资源限制由服务端题库提供。编译与运行错误属于判题结果，通常返回 HTTP 200；网关错误使用 502 / 503 / 504。

当前版本用于学习和本机演示，尚未完成多测试用例、编译超时、精确 MLE 分类、用户程序安全隔离、任务自动清理及用户 / 数据库体系。
