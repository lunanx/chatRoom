-- ============================================================
-- 网络聊天室数据库
-- MySQL 8.0
-- ============================================================

-- 如果已有数据库，可忽略下面两句。
CREATE DATABASE IF NOT EXISTS `chatRoomDB`
    CHARACTER SET utf8mb4
    COLLATE utf8mb4_unicode_ci;

USE `chatRoomDB`;

-- ============================================================
-- 注意：
-- 本脚本不执行 DROP TABLE。
-- 如果需要重置数据库，请先手工备份，再根据实际情况删除旧表。
-- ============================================================


-- ============================================================
-- User P0   ------------10.8   数据库只建立完User，为了注册功能
-- ============================================================

CREATE TABLE IF NOT EXISTS `User`
(
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT
        COMMENT '用户唯一ID',

    `username` VARCHAR(64) NOT NULL
        COMMENT '登录及搜索使用的账号名，必须唯一',

    `password_hash` VARCHAR(255) NOT NULL
        COMMENT '密码哈希值，禁止保存明文密码',

    `nickname` VARCHAR(64) NOT NULL
        COMMENT '用户对外显示的昵称',

    `avatar` VARCHAR(1024) NULL
        COMMENT '头像资源地址',

    `online` TINYINT UNSIGNED NOT NULL DEFAULT 0
        COMMENT '在线状态：0=离线，1=在线',

    `status` TINYINT UNSIGNED NOT NULL DEFAULT 1
        COMMENT '账号状态：1=正常，2=禁用，3=注销',

    `deleted_at` DATETIME(3) NULL
        COMMENT '账号软删除时间',

    `created_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3)
        COMMENT '创建时间',

    `updated_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3)
        ON UPDATE CURRENT_TIMESTAMP(3)
        COMMENT '最后更新时间',

    PRIMARY KEY (`id`),

    CONSTRAINT `uk_User_username`
        UNIQUE (`username`),

    INDEX `idx_User_status` 
            (`status`),

    INDEX `idx_User_online`
         (`online`),

    CONSTRAINT `ck_User_online`
        CHECK (`online` IN (0, 1)),

    CONSTRAINT `ck_User_status`
        CHECK (`status` IN (1, 2, 3))

) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci
  COMMENT='用户账号与基础个人资料';


-- ============================================================
-- Friendship P0
-- ============================================================

CREATE TABLE IF NOT EXISTS `Friendship`
(
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT
        COMMENT '好友关系ID',

    `user_id` BIGINT UNSIGNED NOT NULL
        COMMENT '规范化后的较小用户ID，必须小于friend_id',

    `friend_id` BIGINT UNSIGNED NOT NULL
        COMMENT '规范化后的较大用户ID',

    `status` TINYINT UNSIGNED NOT NULL DEFAULT 1
        COMMENT '好友关系状态：0=已解除/无效，1=有效',

    `created_at` DATETIME(3) NOT NULL
        DEFAULT CURRENT_TIMESTAMP(3)
        COMMENT '好友关系创建时间',

    `updated_at` DATETIME(3) NOT NULL
        DEFAULT CURRENT_TIMESTAMP(3)
        ON UPDATE CURRENT_TIMESTAMP(3)
        COMMENT '好友关系更新时间',

    PRIMARY KEY (`id`),

    UNIQUE KEY `uk_Friendship_user_friend`
        (`user_id`, `friend_id`),

    CONSTRAINT `fk_Friendship_user_id`
        FOREIGN KEY (`user_id`)
        REFERENCES `User` (`id`)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    CONSTRAINT `fk_Friendship_friend_id`
        FOREIGN KEY (`friend_id`)
        REFERENCES `User` (`id`)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    INDEX `idx_Friendship_user_status` --为了直接查询user_id用户的好友
        (`user_id`, `status`),

    INDEX `idx_Friendship_friend_status`--为了直接查询friend_id用户的好友
        (`friend_id`, `status`),

    CONSTRAINT `ck_Friendship_user_order`
        CHECK (`user_id` < `friend_id`),

    CONSTRAINT `ck_Friendship_status`
        CHECK (`status` IN (0, 1))

) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci
  COMMENT='用户之间的好友关系';

--
-- ============================================================
-- 3. FriendRemark P0
-- ============================================================

CREATE TABLE IF NOT EXISTS `FriendRemark`
(
    `user_id` BIGINT UNSIGNED NOT NULL
        COMMENT '设置备注的用户ID',

    `friend_id` BIGINT UNSIGNED NOT NULL
        COMMENT '被设置备注的好友用户ID',

    `remark` VARCHAR(64) NOT NULL
        COMMENT 'user_id用户对friend_id用户设置的备注',

    `created_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3)
        COMMENT '备注创建时间',

    `updated_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3)
        ON UPDATE CURRENT_TIMESTAMP(3)
        COMMENT '备注更新时间',

    PRIMARY KEY (`user_id`, `friend_id`),

    CONSTRAINT `fk_FriendRemark_user_id`
        FOREIGN KEY (`user_id`)
        REFERENCES `User` (`id`)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    CONSTRAINT `fk_FriendRemark_friend_id`
        FOREIGN KEY (`friend_id`)
        REFERENCES `User` (`id`)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    INDEX `idx_FriendRemark_friend_id` --方便直接查询好友的备注
        (`friend_id`),

    CONSTRAINT `ck_FriendRemark_not_self`
        CHECK (`user_id` <> `friend_id`)

) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci
  COMMENT='用户对好友设置的个人备注';

--
-- ============================================================
-- FriendRequest P0
-- ============================================================

CREATE TABLE IF NOT EXISTS `FriendRequest`
(
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT
        COMMENT '好友申请记录ID',

    `sender_id` BIGINT UNSIGNED NOT NULL
        COMMENT '好友申请发送者',

    `receiver_id` BIGINT UNSIGNED NOT NULL
        COMMENT '好友申请接收者',

    `status` TINYINT UNSIGNED NOT NULL DEFAULT 0
        COMMENT '申请状态 0=待处理 1=已同意 2=已拒绝 3=已取消 4=已过期',

    `created_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3)
        COMMENT '申请创建时间',

    `updated_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3)
        ON UPDATE CURRENT_TIMESTAMP(3)
        COMMENT '申请更新时间',

    PRIMARY KEY (`id`),

    CONSTRAINT `fk_FriendRequest_sender_id`
        FOREIGN KEY (`sender_id`)
        REFERENCES `User` (`id`)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    CONSTRAINT `fk_FriendRequest_receiver_id`
        FOREIGN KEY (`receiver_id`)
        REFERENCES `User` (`id`)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    INDEX `idx_FriendRequest_receiver_status`       --用于搭配系统通知接收者，注意通知时，FriendRequest.status与Notification.type的对应
         (`receiver_id`, `status`, `created_at`),

    INDEX `idx_FriendRequest_sender_status`         --用于搭配系统通知发送者，注意通知时，FriendRequest.status与Notification.type的对应
         (`sender_id`, `status`, `created_at`),

    CONSTRAINT `ck_FriendRequest_not_self`
        CHECK (`sender_id` <> `receiver_id`),

    CONSTRAINT `ck_FriendRequest_status`
        CHECK (`status` IN (0, 1, 2, 3, 4))

) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci
  COMMENT='好友申请记录';


-- ============================================================
-- Blacklist P0
-- ============================================================

CREATE TABLE IF NOT EXISTS `Blacklist`
(
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT
        COMMENT '黑名单记录ID',

    `user_id` BIGINT UNSIGNED NOT NULL
        COMMENT '执行拉黑操作的用户ID',

    `blocked_user_id` BIGINT UNSIGNED NOT NULL
        COMMENT '被拉黑的用户ID',

    `created_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3)
        COMMENT '拉黑时间',

    `status` TINYINT UNSIGNED NOT NULL DEFAULT 1 
        COMMENT '1-有效 0-无效/删除',

    `deleted_at` DATETIME(3) NULL 
        COMMENT '记录删除时间',

    PRIMARY KEY (`id`),

    CONSTRAINT `uk_Blacklist_user_blocked`
        UNIQUE (`user_id`, `blocked_user_id`),

    CONSTRAINT `fk_Blacklist_user_id`
        FOREIGN KEY (`user_id`)
        REFERENCES `User` (`id`)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    CONSTRAINT `fk_Blacklist_blocked_user_id`
        FOREIGN KEY (`blocked_user_id`)
        REFERENCES `User` (`id`)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    INDEX `idx_Blacklist_user`
         (`user_id`),

    INDEX `idx_Blacklist_blocked_user`
         (`blocked_user_id`),

    CONSTRAINT `ck_Blacklist_not_self`
        CHECK (`user_id` <> `blocked_user_id`)

    CONSTRAINT `ck_Blacklist_status_deleted`
    CHECK
    (
        (`status` = 1 AND `deleted_at` IS NULL)
        OR
        (`status` = 0 AND `deleted_at` IS NOT NULL)
    )
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci
  COMMENT='用户黑名单关系';


-- ============================================================
-- Group P0
-- ============================================================

CREATE TABLE IF NOT EXISTS `Group`
(
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT
        COMMENT '群组唯一ID',

    `name` VARCHAR(128) NOT NULL
        COMMENT '群名称',

    `avatar` VARCHAR(1024) NULL
        COMMENT '群头像资源地址',

    `owner_id` BIGINT UNSIGNED NOT NULL
        COMMENT '群主用户ID',

    `announcement` TEXT NULL
        COMMENT '当前群公告',

    `status` TINYINT UNSIGNED NOT NULL DEFAULT 1
        COMMENT '群状态：1=正常，2=禁用，3=解散',

    `created_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3)
        COMMENT '群创建时间',

    `updated_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3)
        ON UPDATE CURRENT_TIMESTAMP(3)
        COMMENT '群更新时间',

    PRIMARY KEY (`id`),

    CONSTRAINT `fk_Group_owner_id`
        FOREIGN KEY (`owner_id`)
        REFERENCES `User` (`id`)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    INDEX `idx_Group_owner_id`
         (`owner_id`),

    INDEX `idx_Group_status_created`
         (`status`, `created_at`),

    CONSTRAINT `ck_Group_status`
        CHECK (`status` IN (1, 2, 3))

) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci
  COMMENT='群组基本信息';


-- ============================================================
-- GroupMember P0
-- ============================================================

CREATE TABLE IF NOT EXISTS `GroupMember`
(
    `group_id` BIGINT UNSIGNED NOT NULL
        COMMENT '群组ID',

    `user_id` BIGINT UNSIGNED NOT NULL
        COMMENT '群成员用户ID',

    `role` TINYINT UNSIGNED NOT NULL DEFAULT 3
        COMMENT '群成员角色：1=群主，2=管理员，3=普通成员',

    `muted_until` DATETIME(3) NULL
        COMMENT '禁言截止时间，NULL表示当前未禁言',

    -- 【建议新增】
    `joined_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3)
        COMMENT '加入群组时间',

    -- 【建议新增】
    `updated_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3)
        ON UPDATE CURRENT_TIMESTAMP(3)
        COMMENT '成员记录更新时间',

    PRIMARY KEY (`group_id`, `user_id`),

    CONSTRAINT `fk_GroupMember_group_id`
        FOREIGN KEY (`group_id`)
        REFERENCES `Group` (`id`)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    CONSTRAINT `fk_GroupMember_user_id`
        FOREIGN KEY (`user_id`)
        REFERENCES `User` (`id`)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    INDEX `idx_GroupMember_user_id`
         (`user_id`),

    INDEX `idx_GroupMember_group_role`
         (`group_id`, `role`),

    CONSTRAINT `ck_GroupMember_role`
        CHECK (`role` IN (1, 2, 3))

) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci
  COMMENT='群组成员及成员权限';


-- ============================================================
-- Conversation P0
-- ============================================================

CREATE TABLE IF NOT EXISTS `Conversation`
(
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT
        COMMENT '会话唯一ID',

    `type` TINYINT UNSIGNED NOT NULL
        COMMENT '会话类型：1=单聊，2=群聊',

    `user_id_1` BIGINT UNSIGNED NULL
        COMMENT '单聊第一个用户ID，必须小于user_id_2',

    `user_id_2` BIGINT UNSIGNED NULL
        COMMENT '单聊第二个用户ID',

    `group_id` BIGINT UNSIGNED NULL
        COMMENT '群聊对应群组ID',

    `last_seq` BIGINT UNSIGNED NOT NULL DEFAULT 0
        COMMENT '当前会话已经成功提交的最大消息序号',

    `created_at` DATETIME(3) NOT NULL
        DEFAULT CURRENT_TIMESTAMP(3)
        COMMENT '会话创建时间',

    `updated_at` DATETIME(3) NOT NULL
        DEFAULT CURRENT_TIMESTAMP(3)
        ON UPDATE CURRENT_TIMESTAMP(3)
        COMMENT '会话更新时间',

    PRIMARY KEY (`id`),

    UNIQUE KEY `uk_Conversation_private_users`
        (`type`, `user_id_1`, `user_id_2`),

    UNIQUE KEY `uk_Conversation_group`
        (`type`, `group_id`),

    CONSTRAINT `fk_Conversation_user_id_1`
        FOREIGN KEY (`user_id_1`)
        REFERENCES `User` (`id`)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    CONSTRAINT `fk_Conversation_user_id_2`
        FOREIGN KEY (`user_id_2`)
        REFERENCES `User` (`id`)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    CONSTRAINT `fk_Conversation_group_id`
        FOREIGN KEY (`group_id`)
        REFERENCES `Group` (`id`)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    INDEX `idx_Conversation_user1`
        (`user_id_1`),

    INDEX `idx_Conversation_user2`
        (`user_id_2`),

    INDEX `idx_Conversation_group`
        (`group_id`),

    CONSTRAINT `ck_Conversation_type_data`
        CHECK
        (
            (
                `type` = 1
                AND `user_id_1` IS NOT NULL
                AND `user_id_2` IS NOT NULL
                AND `group_id` IS NULL
                AND `user_id_1` < `user_id_2`
            )
            OR
            (
                `type` = 2
                AND `user_id_1` IS NULL
                AND `user_id_2` IS NULL
                AND `group_id` IS NOT NULL
            )
        ),

    CONSTRAINT `ck_Conversation_users_not_same`
        CHECK
        (
            `user_id_1` IS NULL
            OR `user_id_2` IS NULL
            OR `user_id_1` <> `user_id_2`
        )

) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci
  COMMENT='统一表示单聊和群聊的聊天会话';


-- ============================================================
-- 8. Message P0
-- ============================================================

CREATE TABLE IF NOT EXISTS `Message`
(
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT
        COMMENT '消息数据库内部主键',

    `server_msg_id` BIGINT UNSIGNED NOT NULL
        COMMENT '服务端生成的全局唯一消息ID',

    `client_msg_id` VARCHAR(64) NOT NULL
        COMMENT '客户端生成的幂等ID，重试时必须保持不变',

    `conversation_id` BIGINT UNSIGNED NOT NULL
        COMMENT '所属Conversation ID',

    `sender_id` BIGINT UNSIGNED NOT NULL
        COMMENT '发送者用户ID',

    `seq` BIGINT UNSIGNED NOT NULL
        COMMENT '当前Conversation内按成功提交顺序分配的消息序号',

    `type` TINYINT UNSIGNED NOT NULL
        COMMENT '消息类型：1=文本，2=图片，3=文件，4=语音，5=视频',

    `content` LONGTEXT NULL
        COMMENT '文本或业务扩展内容；非文本消息可为空，附件通过File关联',

    `status` TINYINT UNSIGNED NOT NULL DEFAULT 1
        COMMENT '消息状态：1=正常，2=已撤回，3=已删除',

    `recalled_at` DATETIME(3) NULL
        COMMENT '消息撤回时间',

    `deleted_at` DATETIME(3) NULL
        COMMENT '消息删除时间',

    `created_at` DATETIME(3) NOT NULL
        DEFAULT CURRENT_TIMESTAMP(3)
        COMMENT '消息创建时间',

    `updated_at` DATETIME(3) NOT NULL
        DEFAULT CURRENT_TIMESTAMP(3)
        ON UPDATE CURRENT_TIMESTAMP(3)
        COMMENT '消息更新时间',

    PRIMARY KEY (`id`),

    UNIQUE KEY `uk_Message_server_msg_id`
        (`server_msg_id`),

    UNIQUE KEY `uk_Message_sender_client_msg`
        (`sender_id`, `client_msg_id`),

    UNIQUE KEY `uk_Message_conversation_seq`
        (`conversation_id`, `seq`),

    CONSTRAINT `fk_Message_conversation_id`
        FOREIGN KEY (`conversation_id`)
        REFERENCES `Conversation` (`id`)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    CONSTRAINT `fk_Message_sender_id`
        FOREIGN KEY (`sender_id`)
        REFERENCES `User` (`id`)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    INDEX `idx_Message_conversation_created`
        (`conversation_id`, `created_at`),

    INDEX `idx_Message_sender_created`
        (`sender_id`, `created_at`),

    CONSTRAINT `ck_Message_type`
        CHECK (`type` IN (1, 2, 3, 4, 5)),

    CONSTRAINT `ck_Message_status`
        CHECK (`status` IN (1, 2, 3)),

    CONSTRAINT `ck_Message_recalled_time`
        CHECK
        (
            (`status` <> 2)                 -- 这里的逻辑就是，
                                            -- 如果status!=2,那么recalled_at是不是空都可以留下
            OR (`recalled_at` IS NOT NULL)  -- 如果 status=2,那么只有recalled_at不是空才能保留
        ),                                  -- 看不懂就换成 NOT(status = 2 AND recalled_at IS NULL)

    CONSTRAINT `ck_Message_deleted_time`    --意义同上
        CHECK
        (
            (`status` <> 3)
            OR (`deleted_at` IS NOT NULL)
        )

) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci
  COMMENT='聊天消息';


-- ============================================================
-- 9. ReadCursor P1
-- ============================================================

CREATE TABLE IF NOT EXISTS `ReadCursor`
(
    `user_id` BIGINT UNSIGNED NOT NULL
        COMMENT '用户ID',

    `conversation_id` BIGINT UNSIGNED NOT NULL
        COMMENT '会话ID',

    `read_seq` BIGINT UNSIGNED NOT NULL DEFAULT 0
        COMMENT '用户在该会话已经确认阅读到的最大Message.seq',

    `updated_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3)
        ON UPDATE CURRENT_TIMESTAMP(3)
        COMMENT '已读游标更新时间',

    PRIMARY KEY (`user_id`, `conversation_id`),

    CONSTRAINT `fk_ReadCursor_user_id`
        FOREIGN KEY (`user_id`)
        REFERENCES `User` (`id`)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    CONSTRAINT `fk_ReadCursor_conversation_id`
        FOREIGN KEY (`conversation_id`)
        REFERENCES `Conversation` (`id`)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    INDEX `idx_ReadCursor_conversation`
         (`conversation_id`)

) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci
  COMMENT='用户在各个会话中的已读进度';


-- ============================================================
-- 10. Notification P0
-- ============================================================

CREATE TABLE IF NOT EXISTS `Notification`
(
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT
        COMMENT '通知唯一ID',

    `user_id` BIGINT UNSIGNED NOT NULL
        COMMENT '通知接收用户ID',

    `type` TINYINT UNSIGNED NOT NULL
        COMMENT '通知类型：1=系统，2=好友请求，3=好友处理结果，4=群邀请，5=群更新，6=其他',

    `payload` JSON NOT NULL
        COMMENT '通知业务数据JSON',

    `read_status` TINYINT UNSIGNED NOT NULL DEFAULT 0
        COMMENT '通知阅读状态：0=未读，1=已读',

    `created_at` DATETIME(3) NOT NULL
        DEFAULT CURRENT_TIMESTAMP(3)
        COMMENT '通知创建时间',

    `read_at` DATETIME(3) NULL
        COMMENT '通知阅读时间',

    PRIMARY KEY (`id`),

    CONSTRAINT `fk_Notification_user_id`
        FOREIGN KEY (`user_id`)
        REFERENCES `User` (`id`)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    INDEX `idx_Notification_user_status_created`
        (`user_id`, `read_status`, `created_at`),

    INDEX `idx_Notification_created`
        (`created_at`),

    CONSTRAINT `ck_Notification_type`
        CHECK (`type` IN (1, 2, 3, 4, 5, 6)),

    CONSTRAINT `ck_Notification_read_status`
        CHECK (`read_status` IN (0, 1))

) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci
  COMMENT='系统及业务通知';


-- ============================================================
-- 11. File P0
-- ============================================================

CREATE TABLE IF NOT EXISTS `File`
(
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT
        COMMENT '文件唯一ID',

    `uploader_id` BIGINT UNSIGNED NOT NULL
        COMMENT '文件上传用户ID',

    `message_id` BIGINT UNSIGNED NOT NULL
        COMMENT '所属消息ID，一条消息允许关联多个文件',

    `path` VARCHAR(2048) NOT NULL
        COMMENT '文件存储路径或资源标识',

    `size` BIGINT UNSIGNED NOT NULL
        COMMENT '文件大小，单位字节',

    `type` TINYINT UNSIGNED NOT NULL
        COMMENT '文件类型：1=图片，2=普通文件，3=音频，4=视频',

    `expire_at` DATETIME(3) NULL
        COMMENT '文件过期时间',

    `created_at` DATETIME(3) NOT NULL
        DEFAULT CURRENT_TIMESTAMP(3)
        COMMENT '文件记录创建时间',

    PRIMARY KEY (`id`),

    CONSTRAINT `fk_File_uploader_id`
        FOREIGN KEY (`uploader_id`)
        REFERENCES `User` (`id`)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    CONSTRAINT `fk_File_message_id`
        FOREIGN KEY (`message_id`)
        REFERENCES `Message` (`id`)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    INDEX `idx_File_message_id`
        (`message_id`),

    INDEX `idx_File_uploader_created`
        (`uploader_id`, `created_at`),

    INDEX `idx_File_expire_at`
        (`expire_at`),

    CONSTRAINT `ck_File_type`
        CHECK (`type` IN (1, 2, 3, 4))

) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci
  COMMENT='消息附件与文件资源';


-- ============================================================
-- 12. Report
-- ============================================================

CREATE TABLE IF NOT EXISTS `Report`
(
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT
        COMMENT '举报记录ID',

    `reporter_id` BIGINT UNSIGNED NOT NULL
        COMMENT '举报人用户ID',

    `target_type` TINYINT UNSIGNED NOT NULL
        COMMENT '举报对象类型：1=用户，2=群组，3=消息，4=动态，5=评论',

    `target_id` BIGINT UNSIGNED NOT NULL
        COMMENT '被举报对象ID，多态关联，不建立直接外键',

    `reason` VARCHAR(1000) NOT NULL
        COMMENT '举报原因',

    `status` TINYINT UNSIGNED NOT NULL DEFAULT 0
        COMMENT '处理状态：0=待处理，1=审核中，2=已处理，3=已驳回',

    -- 【建议新增】
    `reviewer_id` BIGINT UNSIGNED NULL
        COMMENT '处理该举报的审核员用户ID',

    -- 【建议新增】
    `resolution` VARCHAR(1000) NULL
        COMMENT '举报处理结果说明',

    -- 【建议新增】
    `created_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3)
        COMMENT '举报创建时间',

    -- 【建议新增】
    `updated_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3)
        ON UPDATE CURRENT_TIMESTAMP(3)
        COMMENT '举报更新时间',

    PRIMARY KEY (`id`),

    CONSTRAINT `fk_Report_reporter_id`
        FOREIGN KEY (`reporter_id`)
        REFERENCES `User` (`id`)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    CONSTRAINT `fk_Report_reviewer_id`
        FOREIGN KEY (`reviewer_id`)
        REFERENCES `User` (`id`)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    INDEX `idx_Report_status_created`
         (`status`, `created_at`),

    INDEX `idx_Report_target`
         (`target_type`, `target_id`),

    INDEX `idx_Report_reporter_created`
         (`reporter_id`, `created_at`),

    CONSTRAINT `ck_Report_target_type`
        CHECK (`target_type` IN (1, 2, 3, 4, 5)),

    CONSTRAINT `ck_Report_status`
        CHECK (`status` IN (0, 1, 2, 3))

) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci
  COMMENT='用户举报记录';


-- ============================================================
-- 13. Device
-- ============================================================

CREATE TABLE IF NOT EXISTS `Device`
(
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT
        COMMENT '设备记录ID',

    `user_id` BIGINT UNSIGNED NOT NULL
        COMMENT '所属用户ID',

    `device_type` TINYINT UNSIGNED NOT NULL
        COMMENT '设备类型：1=桌面端，2=移动端，3=Web，其他值由业务扩展',

    `login_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3)
        COMMENT '最近一次登录时间',

    `last_active_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3)
        COMMENT '最近一次活跃时间',

    PRIMARY KEY (`id`),

    CONSTRAINT `fk_Device_user_id`
        FOREIGN KEY (`user_id`)
        REFERENCES `User` (`id`)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    INDEX `idx_Device_user_active`
         (`user_id`, `last_active_at`),

    INDEX `idx_Device_user_login`
         (`user_id`, `login_at`),

    CONSTRAINT `ck_Device_type`
        CHECK (`device_type` IN (1, 2, 3))

) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci
  COMMENT='用户登录设备记录';



-- ============================================================
-- 14. AuditLog
-- ============================================================

CREATE TABLE IF NOT EXISTS `AuditLog`
(
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT
        COMMENT '审计日志ID',

    `operator_id` BIGINT UNSIGNED NOT NULL
        COMMENT '执行操作的管理员/审核员用户ID',

    `action` VARCHAR(128) NOT NULL
        COMMENT '操作名称',

    `target_type` TINYINT UNSIGNED NULL
        COMMENT '目标类型：1=用户，2=群组，3=消息，4=动态，5=评论，其他值按业务扩展',

    `target_id` BIGINT UNSIGNED NULL
        COMMENT '目标对象ID，多态关联',

    -- 【建议新增】
    `details` JSON NULL
        COMMENT '操作上下文或详细信息JSON',

    `created_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3)
        COMMENT '操作发生时间',

    PRIMARY KEY (`id`),

    CONSTRAINT `fk_AuditLog_operator_id`
        FOREIGN KEY (`operator_id`)
        REFERENCES `User` (`id`)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    INDEX `idx_AuditLog_operator_created`
         (`operator_id`, `created_at`),

    INDEX `idx_AuditLog_target`
         (`target_type`, `target_id`, `created_at`),

    INDEX `idx_AuditLog_created`
         (`created_at`)

) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci
  COMMENT='管理员及审核操作审计日志';


-- ============================================================
-- 15. Dynamic
-- ============================================================

CREATE TABLE IF NOT EXISTS `Dynamic`
(
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT
        COMMENT '动态唯一ID',

    `user_id` BIGINT UNSIGNED NOT NULL
        COMMENT '动态发布者ID',

    `content` LONGTEXT NOT NULL
        COMMENT '动态正文内容',

    `visibility` TINYINT UNSIGNED NOT NULL DEFAULT 1
        COMMENT '可见范围：1=公开，2=好友可见，3=仅自己',

    -- 【建议新增】
    `status` TINYINT UNSIGNED NOT NULL DEFAULT 1
        COMMENT '动态状态：1=正常，2=删除，3=审核隐藏',

    `created_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3)
        COMMENT '动态创建时间',

    -- 【建议新增】
    `updated_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3)
        ON UPDATE CURRENT_TIMESTAMP(3)
        COMMENT '动态更新时间',

    PRIMARY KEY (`id`),

    CONSTRAINT `fk_Dynamic_user_id`
        FOREIGN KEY (`user_id`)
        REFERENCES `User` (`id`)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    INDEX `idx_Dynamic_user_created`
         (`user_id`, `created_at`),

    INDEX `idx_Dynamic_visibility_created`
         (`visibility`, `created_at`),

    INDEX `idx_Dynamic_status_created`
         (`status`, `created_at`),

    CONSTRAINT `ck_Dynamic_visibility`
        CHECK (`visibility` IN (1, 2, 3)),

    CONSTRAINT `ck_Dynamic_status`
        CHECK (`status` IN (1, 2, 3))

) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci
  COMMENT='P2用户动态';


-- ============================================================
-- 16. Comment
-- ============================================================

CREATE TABLE IF NOT EXISTS `Comment`
(
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT
        COMMENT '评论唯一ID',

    `dynamic_id` BIGINT UNSIGNED NOT NULL
        COMMENT '所属动态ID',

    `user_id` BIGINT UNSIGNED NOT NULL
        COMMENT '评论发布者ID',

    `content` TEXT NOT NULL
        COMMENT '评论内容',

    `created_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3)
        COMMENT '评论创建时间',

    PRIMARY KEY (`id`),

    CONSTRAINT `fk_Comment_dynamic_id`
        FOREIGN KEY (`dynamic_id`)
        REFERENCES `Dynamic` (`id`)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    CONSTRAINT `fk_Comment_user_id`
        FOREIGN KEY (`user_id`)
        REFERENCES `User` (`id`)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    INDEX `idx_Comment_dynamic_created`
         (`dynamic_id`, `created_at`),

    INDEX `idx_Comment_user_created`
         (`user_id`, `created_at`)

) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci
  COMMENT='P2动态评论';


-- ============================================================
-- 17. CallRecord
-- ============================================================

CREATE TABLE IF NOT EXISTS `CallRecord`
(
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT
        COMMENT '通话记录ID',

    `caller_id` BIGINT UNSIGNED NOT NULL
        COMMENT '发起通话的用户ID',

    `callee_id` BIGINT UNSIGNED NULL
        COMMENT '一对一通话接收者ID；群通话时可为空',

    -- 【建议新增：支持群通话】
    `conversation_id` BIGINT UNSIGNED NULL
        COMMENT '关联会话ID，可统一支持单聊和群聊通话',

    `type` TINYINT UNSIGNED NOT NULL
        COMMENT '通话类型：1=语音，2=视频，3=群语音，4=群视频',

    `status` TINYINT UNSIGNED NOT NULL DEFAULT 1
        COMMENT '通话状态：1=邀请，2=响铃，3=接通，4=结束，5=未接，6=拒绝，7=失败',

    `started_at` DATETIME(3) NULL
        COMMENT '通话开始时间',

    `ended_at` DATETIME(3) NULL
        COMMENT '通话结束时间',

    PRIMARY KEY (`id`),

    CONSTRAINT `fk_CallRecord_caller_id`
        FOREIGN KEY (`caller_id`)
        REFERENCES `User` (`id`)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    CONSTRAINT `fk_CallRecord_callee_id`
        FOREIGN KEY (`callee_id`)
        REFERENCES `User` (`id`)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    CONSTRAINT `fk_CallRecord_conversation_id`
        FOREIGN KEY (`conversation_id`)
        REFERENCES `Conversation` (`id`)
        ON DELETE RESTRICT
        ON UPDATE CASCADE,

    INDEX `idx_CallRecord_caller_started`
         (`caller_id`, `started_at`),

    INDEX `idx_CallRecord_callee_started`
         (`callee_id`, `started_at`),

    INDEX `idx_CallRecord_conversation_started`
         (`conversation_id`, `started_at`),

    CONSTRAINT `ck_CallRecord_type`
        CHECK (`type` IN (1, 2, 3, 4)),

    CONSTRAINT `ck_CallRecord_status`
        CHECK (`status` IN (1, 2, 3, 4, 5, 6, 7))

) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci
  COMMENT='P2音视频通话记录';
