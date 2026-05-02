# 五子棋微服务架构 v2

## 架构概览

```
┌─────────────────────────────────────────────────────────────────┐
│                        Nginx Gateway (:8000)                     │
│                     (路由分发 + WebSocket 代理)                   │
└─────────────────────────────────────────────────────────────────┘
                    │              │              │
          ┌─────────▼──┐  ┌────────▼──┐  ┌───────▼─────┐
          │   认证服务  │  │   匹配服务 │  │   游戏服务   │
          │   :8001    │  │   :8002   │  │    :8003   │
          │            │  │           │  │             │
          │ 注册/登录  │  │ 匹配队列  │  │ WebSocket  │
          │ Token验证  │  │ 创建房间  │  │ 落子转发   │
          │ Cache-Aside│  │ 邀请码   │  │ 退出通知   │
          └─────┬──────┘  └─────┬─────┘  └──────┬─────┘
                │               │                │
          ┌─────▼───────────────▼────────────────▼─────┐
          │                   Redis                     │
          │   用户缓存   │   匹配队列   │   房间状态    │
          └─────────────────────────────────────────────┘
                              │
                      ┌───────▼───────┐
                      │    MySQL      │
                      │   用户表      │
                      └───────────────┘
```

## 目录结构

```
wuziqi-v2/
├── common/                 # 公共库
│   ├── include/           # 头文件
│   │   ├── redis_pool.h   # Redis 连接池
│   │   ├── mysql_pool.h  # MySQL 连接池
│   │   ├── thread_pool.h # 线程池
│   │   ├── http_client.h # HTTP 客户端
│   │   ├── logger.h      # 日志
│   │   └── utils.h       # 工具函数
│   └── src/               # 实现
│
├── auth-service/          # 认证服务
│   ├── src/
│   │   ├── main.cpp      # 入口
│   │   └── handler.cpp   # 业务逻辑
│   └── CMakeLists.txt
│
├── match-service/          # 匹配服务
│   ├── src/
│   ├── CMakeLists.txt
│
├── game-service/           # 游戏服务
│   ├── src/
│   │   ├── main.cpp       # WebSocket 服务器
│   │   ├── handler.cpp    # 消息处理
│   │   └── connection_mgr.cpp  # 连接管理
│   └── CMakeLists.txt
│
├── nginx.conf             # 网关配置
├── docker-compose.yml     # 容器编排
└── scripts/
    └── start.sh           # 启动脚本
```

## 构建

```bash
# 创建构建目录
mkdir build && cd build

# 生成构建文件
cmake ..

# 编译
make -j4

# 或使用 Makefile
cd ..
make -j4
```

## 运行

### 方式一：直接运行

```bash
# 设置环境变量
export REDIS_HOST=127.0.0.1
export REDIS_PORT=6379
export MYSQL_HOST=127.0.0.1
export MYSQL_PORT=3306
export MYSQL_DATABASE=wuziqi

# 启动服务
./scripts/start.sh start
```

### 方式二：Docker Compose

```bash
docker-compose up -d
```

## 服务接口

### 认证服务 (8001)

| 接口 | 方法 | 说明 |
|------|------|------|
| `/api/register` | POST | 用户注册 |
| `/api/login` | POST | 用户登录 |
| `/api/verify` | POST | 验证 Token |
| `/api/refresh` | POST | 刷新 Token |
| `/health` | GET | 健康检查 |

### 匹配服务 (8002)

| 接口 | 方法 | 说明 |
|------|------|------|
| `/api/random_match` | POST | 加入随机匹配 |
| `/api/cancel_match` | POST | 取消匹配 |
| `/api/create_room` | POST | 创建房间 |
| `/api/join_room` | POST | 加入房间 |
| `/api/quit_room` | POST | 退出房间 |

### 游戏服务 (8003)

| 接口 | 说明 |
|------|------|
| `/ws` | WebSocket 连接 |
| `/health` | 健康检查 |
| `/internal/room_ready` | 内部接口：房间就绪通知 |

## WebSocket 消息协议

### 客户端 → 服务器

```json
// 登录
{"type":"login","data":{"token":"xxx"}}

// 落子
{"type":"move","data":{"room_id":"123456","x":5,"y":10}}

// 退出房间
{"type":"quit_room","data":{"room_id":"123456"}}
```

### 服务器 → 客户端

```json
// 登录成功
{"type":"login_success","success":true,"data":{"user_id":1}}

// 房间就绪
{"type":"room_ready","success":true,"data":{"room_id":"123456","player1":1,"player2":2}}

// 落子确认
{"type":"move_received","success":true,"data":{"x":5,"y":10}}

// 对手落子
{"type":"opponent_move","success":true,"data":{"x":6,"y":10}}

// 对手退出
{"type":"opponent_quit","success":true,"data":{"room_id":"123456"}}
```

## 线程池规格

| 服务 | 线程数 | 适用场景 |
|------|--------|----------|
| auth-service | 8 | 登录/注册/Token验证 |
| match-service | 4 | 匹配队列操作（低频） |
| game-service | 16 | 消息处理/落子转发（高频） |

## Redis 数据结构

```
# 用户缓存
user:{id} -> JSON {id, username, password_hash, created_at}
TTL: 3600s

# 会话
session:{user_id} -> token
TTL: 7天

# 匹配队列
match:queue -> List [user_id1, user_id2, ...]

# 房间
room:{room_id} -> Hash {player1, player2, status, invite_code, current_turn}
roomset:{user_id} -> Set [room_id, ...]

# 用户房间映射
user:room:{user_id} -> room_id
```

## 设计亮点

1. **Cache-Aside 模式**: auth-service 先查 Redis，未命中再查 MySQL 并回填
2. **线程池隔离**: 每个服务独立线程池，高峰期互不影响
3. **连接池复用**: Redis/MySQL 连接池，避免频繁创建销毁
4. **无状态服务**: 游戏服务只维护连接和房间映射，支持水平扩展
