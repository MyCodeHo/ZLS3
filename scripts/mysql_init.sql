-- 数据库初始化
CREATE DATABASE IF NOT EXISTS minis3 DEFAULT CHARSET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE minis3;

-- buckets 表（桶元数据）
CREATE TABLE buckets (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(63) NOT NULL UNIQUE,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_name (name)
) ENGINE=InnoDB;

-- CAS blobs 表（内容寻址存储与引用计数）
CREATE TABLE cas_blobs (
    cas_key CHAR(64) PRIMARY KEY,  -- SHA256 hex
    size BIGINT UNSIGNED NOT NULL,
    ref_count INT UNSIGNED NOT NULL DEFAULT 1,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    INDEX idx_ref_count (ref_count),
    INDEX idx_updated_at (updated_at)
) ENGINE=InnoDB;

-- objects 表（对象元数据）
CREATE TABLE objects (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    bucket_id BIGINT UNSIGNED NOT NULL,
    object_key VARCHAR(1024) NOT NULL,
    cas_key CHAR(64) NOT NULL,
    size BIGINT UNSIGNED NOT NULL,
    content_type VARCHAR(255) DEFAULT 'application/octet-stream',
    etag VARCHAR(64) NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    UNIQUE KEY uk_bucket_key (bucket_id, object_key(255)),
    INDEX idx_cas_key (cas_key),
    INDEX idx_created_at (created_at),
    FOREIGN KEY (bucket_id) REFERENCES buckets(id) ON DELETE CASCADE,
    FOREIGN KEY (cas_key) REFERENCES cas_blobs(cas_key)
) ENGINE=InnoDB;

-- multipart_uploads 表（分片上传会话）
CREATE TABLE multipart_uploads (
    upload_id CHAR(36) PRIMARY KEY,  -- UUID
    bucket_id BIGINT UNSIGNED NOT NULL,
    object_key VARCHAR(1024) NOT NULL,
    content_type VARCHAR(255) DEFAULT 'application/octet-stream',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    expires_at TIMESTAMP NOT NULL,
    INDEX idx_bucket_key (bucket_id, object_key(255)),
    INDEX idx_expires_at (expires_at),
    FOREIGN KEY (bucket_id) REFERENCES buckets(id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- multipart_parts 表（分片元数据）
CREATE TABLE multipart_parts (
    upload_id CHAR(36) NOT NULL,
    part_number INT UNSIGNED NOT NULL,
    cas_key CHAR(64) NOT NULL,
    size BIGINT UNSIGNED NOT NULL,
    etag VARCHAR(64) NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (upload_id, part_number),
    FOREIGN KEY (upload_id) REFERENCES multipart_uploads(upload_id) ON DELETE CASCADE,
    FOREIGN KEY (cas_key) REFERENCES cas_blobs(cas_key)
) ENGINE=InnoDB;

-- idempotency_records 表（幂等性记录）
CREATE TABLE idempotency_records (
    idempotency_key VARCHAR(64) PRIMARY KEY,
    request_hash CHAR(64) NOT NULL,
    response_status INT NOT NULL,
    response_body TEXT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    expires_at TIMESTAMP NOT NULL,
    INDEX idx_expires_at (expires_at)
) ENGINE=InnoDB;

-- api_keys 表（简单鉴权）
CREATE TABLE api_keys (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    key_id VARCHAR(32) NOT NULL UNIQUE,
    key_secret VARCHAR(64) NOT NULL,
    name VARCHAR(255),
    enabled BOOLEAN DEFAULT TRUE,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_key_id (key_id)
) ENGINE=InnoDB;

-- 插入默认测试 API Key
INSERT INTO api_keys (key_id, key_secret, name) VALUES 
('test-key-id', 'test-key-secret', 'Test API Key');

-- 插入默认 bucket
INSERT INTO buckets (name) VALUES ('default');
