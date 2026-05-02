#!/bin/bash

# 五子棋微服务启动脚本

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 检查依赖
check_dependencies() {
    log_info "检查依赖..."

    # 检查 Redis
    if ! command -v redis-cli &> /dev/null; then
        log_warn "redis-cli 未安装，跳过 Redis 检查"
    else
        redis-cli ping &> /dev/null && log_info "Redis 连接正常" || log_warn "Redis 未运行"
    fi

    # 检查 MySQL
    if ! command -v mysql &> /dev/null; then
        log_warn "mysql 未安装，跳过 MySQL 检查"
    else
        log_info "MySQL 客户端已安装"
    fi
}

# 启动服务
start_service() {
    local name=$1
    local port=$2
    local binary=$3

    log_info "启动 $name..."

    if [ ! -f "$binary" ]; then
        log_error "$name 可执行文件不存在: $binary"
        return 1
    fi

    # 检查端口是否被占用
    if netstat -tuln 2>/dev/null | grep -q ":$port " || ss -tuln 2>/dev/null | grep -q ":$port "; then
        log_warn "$name 端口 $port 已被占用，跳过"
        return 0
    fi

    # 后台启动
    nohup ./$binary > logs/$name.log 2>&1 &
    echo $! > logs/$name.pid

    sleep 1

    # 检查是否启动成功
    if ps -p $! > /dev/null 2>&1; then
        log_info "$name 启动成功 (PID: $(cat logs/$name.pid))"
    else
        log_error "$name 启动失败，查看日志: logs/$name.log"
    fi
}

# 停止服务
stop_service() {
    local name=$1

    if [ -f "logs/$name.pid" ]; then
        local pid=$(cat logs/$name.pid)
        if ps -p $pid > /dev/null 2>&1; then
            log_info "停止 $name (PID: $pid)..."
            kill $pid
            rm -f logs/$name.pid
        fi
    fi
}

# 主函数
case "${1:-start}" in
    start)
        log_info "========== 启动五子棋微服务 =========="

        check_dependencies

        # 创建日志目录
        mkdir -p logs

        # 设置环境变量
        export REDIS_HOST=${REDIS_HOST:-127.0.0.1}
        export REDIS_PORT=${REDIS_PORT:-6379}
        export MYSQL_HOST=${MYSQL_HOST:-127.0.0.1}
        export MYSQL_PORT=${MYSQL_PORT:-3306}
        export MYSQL_USER=${MYSQL_USER:-root}
        export MYSQL_PASSWORD=${MYSQL_PASSWORD:-}
        export MYSQL_DATABASE=${MYSQL_DATABASE:-wuziqi}
        export AUTH_SERVICE_URL=${AUTH_SERVICE_URL:-http://127.0.0.1:8001}
        export GAME_SERVICE_URL=${GAME_SERVICE_URL:-http://127.0.0.1:8003}

        # 启动各个服务
        start_service "auth-service" 8001 "./auth-service"
        sleep 1
        start_service "match-service" 8002 "./match-service"
        sleep 1
        start_service "game-service" 8003 "./game-service"

        log_info "========== 所有服务已启动 =========="
        log_info "auth-service:  http://127.0.0.1:8001"
        log_info "match-service: http://127.0.0.1:8002"
        log_info "game-service:  ws://127.0.0.1:8003"
        log_info "Nginx 网关:    http://127.0.0.1:8000"
        ;;

    stop)
        log_info "========== 停止五子棋微服务 =========="
        stop_service "game-service"
        stop_service "match-service"
        stop_service "auth-service"
        log_info "========== 所有服务已停止 =========="
        ;;

    restart)
        $0 stop
        sleep 2
        $0 start
        ;;

    status)
        log_info "========== 服务状态 =========="
        for name in auth-service match-service game-service; do
            if [ -f "logs/$name.pid" ]; then
                pid=$(cat logs/$name.pid)
                if ps -p $pid > /dev/null 2>&1; then
                    echo -e "$name: ${GREEN}运行中${NC} (PID: $pid)"
                else
                    echo -e "$name: ${RED}已停止${NC} (PID 文件存在但进程已退出)"
                fi
            else
                echo -e "$name: ${RED}未运行${NC}"
            fi
        done
        ;;

    logs)
        if [ -n "$2" ]; then
            tail -f logs/$2.log
        else
            tail -f logs/*.log
        fi
        ;;

    *)
        echo "用法: $0 {start|stop|restart|status|logs [service]}"
        exit 1
        ;;
esac
