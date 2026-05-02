#include "connection_mgr.h"
#include "logger.h"
#include <unistd.h>
#include <sys/socket.h>
#include <cstring>
#include <cerrno>

void ConnectionManager::addConnection(int fd) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto conn = std::make_shared<WsConnection>(fd);
    fd_to_conn_[fd] = conn;

    LOG_INFO("WebSocket 连接建立: fd=%d", fd);
}

void ConnectionManager::removeConnection(int fd) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = fd_to_conn_.find(fd);
    if (it != fd_to_conn_.end()) {
        auto conn = it->second;

        // 如果用户已绑定，先清理映射
        if (conn->user_id > 0) {
            user_to_fd_.erase(conn->user_id);

            // 清理房间映射
            auto room_it = user_to_room_.find(conn->user_id);
            if (room_it != user_to_room_.end()) {
                std::string room_id = room_it->second;
                room_to_users_[room_id].erase(conn->user_id);
                if (room_to_users_[room_id].empty()) {
                    room_to_users_.erase(room_id);
                }
                user_to_room_.erase(room_it);
            }
        }

        LOG_INFO("WebSocket 连接关闭: fd=%d, user_id=%d", fd, conn->user_id);
        fd_to_conn_.erase(it);
    }
}

void ConnectionManager::bindUser(int fd, int user_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 如果该用户已有其他连接，先断开
    auto old_fd_it = user_to_fd_.find(user_id);
    if (old_fd_it != user_to_fd_.end()) {
        int old_fd = old_fd_it->second;
        auto old_conn_it = fd_to_conn_.find(old_fd);
        if (old_conn_it != fd_to_conn_.end()) {
            old_conn_it->second->authenticated = false;
            old_conn_it->second->user_id = 0;
        }
        user_to_fd_.erase(old_fd_it);
        LOG_INFO("用户 %d 在其他设备登录，旧连接 %d 已失效", user_id, old_fd);
    }

    // 绑定新连接
    auto conn_it = fd_to_conn_.find(fd);
    if (conn_it != fd_to_conn_.end()) {
        conn_it->second->user_id = user_id;
        conn_it->second->authenticated = true;
    }
    user_to_fd_[user_id] = fd;

    LOG_INFO("用户绑定: user_id=%d, fd=%d", user_id, fd);
}

int ConnectionManager::getConnectionFd(int user_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = user_to_fd_.find(user_id);
    if (it != user_to_fd_.end()) {
        return it->second;
    }
    return -1;
}

int ConnectionManager::getUserId(int fd) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = fd_to_conn_.find(fd);
    if (it != fd_to_conn_.end()) {
        return it->second->user_id;
    }
    return 0;
}

bool ConnectionManager::isUserOnline(int user_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return user_to_fd_.find(user_id) != user_to_fd_.end();
}

void ConnectionManager::broadcastToRoom(const std::string& room_id,
                                        const std::string& message,
                                        int exclude_user_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = room_to_users_.find(room_id);
    if (it == room_to_users_.end()) return;

    for (int user_id : it->second) {
        if (user_id == exclude_user_id) continue;

        auto fd_it = user_to_fd_.find(user_id);
        if (fd_it != user_to_fd_.end()) {
            sendToFd(fd_it->second, message);
        }
    }
}

void ConnectionManager::setUserRoom(int user_id, const std::string& room_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 从旧房间移除
    auto old_room_it = user_to_room_.find(user_id);
    if (old_room_it != user_to_room_.end()) {
        room_to_users_[old_room_it->second].erase(user_id);
    }

    // 加入新房间
    user_to_room_[user_id] = room_id;
    room_to_users_[room_id].insert(user_id);

    LOG_DEBUG("用户 %d 加入房间 %s", user_id, room_id.c_str());
}

void ConnectionManager::removeUserRoom(int user_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = user_to_room_.find(user_id);
    if (it != user_to_room_.end()) {
        std::string room_id = it->second;
        room_to_users_[room_id].erase(user_id);
        if (room_to_users_[room_id].empty()) {
            room_to_users_.erase(room_id);
        }
        user_to_room_.erase(it);
    }
}

std::string ConnectionManager::getUserRoom(int user_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = user_to_room_.find(user_id);
    if (it != user_to_room_.end()) {
        return it->second;
    }
    return "";
}

bool ConnectionManager::sendToUser(int user_id, const std::string& message) {
    int fd = getConnectionFd(user_id);
    if (fd < 0) {
        LOG_WARN("发送消息失败，用户不在线: user_id=%d", user_id);
        return false;
    }
    return sendToFd(fd, message);
}

bool ConnectionManager::sendToFd(int fd, const std::string& message) {
    ssize_t n = send(fd, message.c_str(), message.length(), 0);
    if (n < 0) {
        LOG_ERROR("发送消息失败: fd=%d, errno=%d (%s)", fd, errno, strerror(errno));
        return false;
    } else if (n < (ssize_t)message.length()) {
        LOG_WARN("发送消息不完整: fd=%d, sent=%d, expected=%d", fd, (int)n, (int)message.length());
    }
    LOG_INFO("sendToFd 成功: fd=%d, len=%d", fd, (int)message.length());
    return true;
}

size_t ConnectionManager::getOnlineCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return user_to_fd_.size();
}
