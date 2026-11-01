-- ============================================================================
-- Operating System-Based Train Ticket Reservation System
-- Relational Database Schema (3NF Normalized, MySQL / MariaDB Compatible)
-- Campus / Division: Durgapur (DGR) / Asansol (ASN), Eastern Railway (ER)
-- ============================================================================

CREATE DATABASE IF NOT EXISTS `train_reservation_db` 
    CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

USE `train_reservation_db`;

-- Drop existing tables in reverse dependency order
SET FOREIGN_KEY_CHECKS = 0;
DROP TABLE IF EXISTS `audit_logs`;
DROP TABLE IF EXISTS `transactions`;
DROP TABLE IF EXISTS `cancellations`;
DROP TABLE IF EXISTS `passengers`;
DROP TABLE IF EXISTS `bookings`;
DROP TABLE IF EXISTS `seat_inventory`;
DROP TABLE IF EXISTS `coaches`;
DROP TABLE IF EXISTS `route_stops`;
DROP TABLE IF EXISTS `trains`;
DROP TABLE IF EXISTS `stations`;
DROP TABLE IF EXISTS `users`;
SET FOREIGN_KEY_CHECKS = 1;

-- ----------------------------------------------------------------------------
-- Table 1: users (Authentication, RBAC & Counter Profiles)
-- ----------------------------------------------------------------------------
CREATE TABLE `users` (
    `user_id` INT AUTO_INCREMENT PRIMARY KEY,
    `username` VARCHAR(32) NOT NULL UNIQUE,
    `password_hash` CHAR(64) NOT NULL,
    `salt` CHAR(32) NOT NULL,
    `full_name` VARCHAR(64) NOT NULL,
    `role` ENUM('PASSENGER', 'BOOKING_AGENT', 'ADMINISTRATOR', 'TTE_CONDUCTOR') NOT NULL DEFAULT 'PASSENGER',
    `phone` VARCHAR(20) NOT NULL,
    `email` VARCHAR(64) NOT NULL,
    `station_code` VARCHAR(8) DEFAULT 'DGR',
    `is_active` BOOLEAN NOT NULL DEFAULT TRUE,
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    INDEX `idx_users_role` (`role`),
    INDEX `idx_users_station` (`station_code`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ----------------------------------------------------------------------------
-- Table 2: stations (Railway Station Network)
-- ----------------------------------------------------------------------------
CREATE TABLE `stations` (
    `station_code` VARCHAR(8) PRIMARY KEY,
    `station_name` VARCHAR(64) NOT NULL,
    `division` VARCHAR(32) NOT NULL,
    `zone` VARCHAR(32) NOT NULL,
    `platform_count` INT NOT NULL DEFAULT 4
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ----------------------------------------------------------------------------
-- Table 3: trains (Train Catalog & Operating Specs)
-- ----------------------------------------------------------------------------
CREATE TABLE `trains` (
    `train_no` INT PRIMARY KEY,
    `train_name` VARCHAR(64) NOT NULL,
    `source_code` VARCHAR(8) NOT NULL,
    `dest_code` VARCHAR(8) NOT NULL,
    `total_distance_km` INT NOT NULL,
    `departure_time` VARCHAR(8) NOT NULL,
    `arrival_time` VARCHAR(8) NOT NULL,
    `is_superfast` BOOLEAN NOT NULL DEFAULT TRUE,
    `base_rate_per_km` DECIMAL(6,2) NOT NULL DEFAULT 0.65,
    `capacity_1a` INT NOT NULL DEFAULT 0,
    `capacity_2a` INT NOT NULL DEFAULT 0,
    `capacity_3a` INT NOT NULL DEFAULT 0,
    `capacity_cc` INT NOT NULL DEFAULT 0,
    `capacity_sl` INT NOT NULL DEFAULT 0,
    `capacity_2s` INT NOT NULL DEFAULT 0,
    `rac_limit` INT NOT NULL DEFAULT 20,
    `wl_limit` INT NOT NULL DEFAULT 40,
    `is_active` BOOLEAN NOT NULL DEFAULT TRUE,
    FOREIGN KEY (`source_code`) REFERENCES `stations` (`station_code`),
    FOREIGN KEY (`dest_code`) REFERENCES `stations` (`station_code`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ----------------------------------------------------------------------------
-- Table 4: route_stops (Train Stoppages and Timetable)
-- ----------------------------------------------------------------------------
CREATE TABLE `route_stops` (
    `id` INT AUTO_INCREMENT PRIMARY KEY,
    `train_no` INT NOT NULL,
    `station_code` VARCHAR(8) NOT NULL,
    `stop_sequence` INT NOT NULL,
    `distance_from_source_km` INT NOT NULL,
    `arr_time` VARCHAR(8) NOT NULL,
    `dep_time` VARCHAR(8) NOT NULL,
    `halt_minutes` INT NOT NULL DEFAULT 2,
    UNIQUE KEY `uk_train_seq` (`train_no`, `stop_sequence`),
    FOREIGN KEY (`train_no`) REFERENCES `trains` (`train_no`) ON DELETE CASCADE,
    FOREIGN KEY (`station_code`) REFERENCES `stations` (`station_code`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ----------------------------------------------------------------------------
-- Table 5: coaches (Coach Master Config)
-- ----------------------------------------------------------------------------
CREATE TABLE `coaches` (
    `coach_id` INT AUTO_INCREMENT PRIMARY KEY,
    `train_no` INT NOT NULL,
    `coach_code` VARCHAR(8) NOT NULL,
    `travel_class` ENUM('1A', '2A', '3A', 'CC', 'SL', '2S') NOT NULL,
    `total_seats` INT NOT NULL DEFAULT 72,
    FOREIGN KEY (`train_no`) REFERENCES `trains` (`train_no`) ON DELETE CASCADE,
    INDEX `idx_coach_train` (`train_no`, `travel_class`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ----------------------------------------------------------------------------
-- Table 6: seat_inventory (Berth Level Allocation & Concurrency Tracking)
-- ----------------------------------------------------------------------------
CREATE TABLE `seat_inventory` (
    `inventory_id` INT AUTO_INCREMENT PRIMARY KEY,
    `train_no` INT NOT NULL,
    `travel_date` DATE NOT NULL,
    `travel_class` ENUM('1A', '2A', '3A', 'CC', 'SL', '2S') NOT NULL,
    `coach_code` VARCHAR(8) NOT NULL,
    `seat_number` INT NOT NULL,
    `berth_type` ENUM('LOWER', 'MIDDLE', 'UPPER', 'SIDE_LOWER', 'SIDE_UPPER', 'WINDOW', 'AISLE', 'CABIN') NOT NULL,
    `is_booked` BOOLEAN NOT NULL DEFAULT FALSE,
    `booking_id` INT DEFAULT NULL,
    `pnr` VARCHAR(16) DEFAULT NULL,
    UNIQUE KEY `uk_train_date_seat` (`train_no`, `travel_date`, `coach_code`, `seat_number`),
    INDEX `idx_inv_avail` (`train_no`, `travel_date`, `travel_class`, `is_booked`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ----------------------------------------------------------------------------
-- Table 7: bookings (Reservation Records & PNR Ledger)
-- ----------------------------------------------------------------------------
CREATE TABLE `bookings` (
    `booking_id` INT AUTO_INCREMENT PRIMARY KEY,
    `pnr` VARCHAR(16) NOT NULL UNIQUE,
    `user_id` INT NOT NULL,
    `train_no` INT NOT NULL,
    `source_code` VARCHAR(8) NOT NULL,
    `dest_code` VARCHAR(8) NOT NULL,
    `travel_date` DATE NOT NULL,
    `travel_class` ENUM('1A', '2A', '3A', 'CC', 'SL', '2S') NOT NULL,
    `quota` ENUM('GENERAL', 'TATKAL', 'LADIES', 'SENIOR', 'DEFENSE') NOT NULL DEFAULT 'GENERAL',
    `passenger_count` INT NOT NULL DEFAULT 1,
    `base_fare` DECIMAL(10,2) NOT NULL,
    `surge_amount` DECIMAL(10,2) NOT NULL DEFAULT 0.00,
    `tatkal_charge` DECIMAL(10,2) NOT NULL DEFAULT 0.00,
    `discount_amount` DECIMAL(10,2) NOT NULL DEFAULT 0.00,
    `gst_amount` DECIMAL(10,2) NOT NULL DEFAULT 0.00,
    `total_fare` DECIMAL(10,2) NOT NULL,
    `overall_status` ENUM('CNF', 'RAC', 'WL', 'CANCELLED') NOT NULL DEFAULT 'CNF',
    `booking_timestamp` TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    `is_cancelled` BOOLEAN NOT NULL DEFAULT FALSE,
    `cancelled_timestamp` TIMESTAMP NULL DEFAULT NULL,
    FOREIGN KEY (`user_id`) REFERENCES `users` (`user_id`),
    FOREIGN KEY (`train_no`) REFERENCES `trains` (`train_no`),
    INDEX `idx_bookings_pnr` (`pnr`),
    INDEX `idx_bookings_user` (`user_id`),
    INDEX `idx_bookings_date` (`travel_date`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ----------------------------------------------------------------------------
-- Table 8: passengers (Individual Ticket Passengers)
-- ----------------------------------------------------------------------------
CREATE TABLE `passengers` (
    `passenger_id` INT AUTO_INCREMENT PRIMARY KEY,
    `booking_id` INT NOT NULL,
    `pnr` VARCHAR(16) NOT NULL,
    `name` VARCHAR(64) NOT NULL,
    `age` INT NOT NULL,
    `gender` ENUM('M', 'F', 'O') NOT NULL,
    `preferred_berth` VARCHAR(16) DEFAULT 'LOWER',
    `allocated_berth` VARCHAR(16) DEFAULT 'LOWER',
    `coach_code` VARCHAR(8) DEFAULT '',
    `seat_number` INT DEFAULT 0,
    `status` ENUM('CNF', 'RAC', 'WL', 'CANCELLED') NOT NULL DEFAULT 'CNF',
    `waitlist_number` INT DEFAULT 0,
    `concession` VARCHAR(32) DEFAULT 'NONE',
    `individual_fare` DECIMAL(10,2) NOT NULL,
    FOREIGN KEY (`booking_id`) REFERENCES `bookings` (`booking_id`) ON DELETE CASCADE,
    INDEX `idx_pass_pnr` (`pnr`),
    INDEX `idx_pass_status` (`status`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ----------------------------------------------------------------------------
-- Table 9: cancellations (Refund & Cancellation Records)
-- ----------------------------------------------------------------------------
CREATE TABLE `cancellations` (
    `cancel_id` INT AUTO_INCREMENT PRIMARY KEY,
    `pnr` VARCHAR(16) NOT NULL,
    `passenger_id` INT DEFAULT NULL,
    `user_id` INT NOT NULL,
    `cancelled_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    `original_fare` DECIMAL(10,2) NOT NULL,
    `cancellation_penalty` DECIMAL(10,2) NOT NULL,
    `refund_amount` DECIMAL(10,2) NOT NULL,
    `reason` VARCHAR(128) DEFAULT 'Passenger Request',
    FOREIGN KEY (`user_id`) REFERENCES `users` (`user_id`),
    INDEX `idx_canc_pnr` (`pnr`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ----------------------------------------------------------------------------
-- Table 10: transactions (Payment & Ledger Tracking)
-- ----------------------------------------------------------------------------
CREATE TABLE `transactions` (
    `tx_id` INT AUTO_INCREMENT PRIMARY KEY,
    `pnr` VARCHAR(16) NOT NULL,
    `user_id` INT NOT NULL,
    `tx_type` ENUM('BOOKING', 'CANCELLATION', 'TATKAL_SURCHARGE', 'REFUND') NOT NULL,
    `amount` DECIMAL(10,2) NOT NULL,
    `timestamp` TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    `payment_mode` VARCHAR(32) DEFAULT 'CASH_COUNTER',
    `remarks` VARCHAR(128) DEFAULT '',
    FOREIGN KEY (`user_id`) REFERENCES `users` (`user_id`),
    INDEX `idx_tx_pnr` (`pnr`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ----------------------------------------------------------------------------
-- Table 11: audit_logs (Operating System Security & Concurrency Audit)
-- ----------------------------------------------------------------------------
CREATE TABLE `audit_logs` (
    `log_id` INT AUTO_INCREMENT PRIMARY KEY,
    `timestamp` TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    `user_id` INT NOT NULL,
    `action` VARCHAR(32) NOT NULL,
    `pnr` VARCHAR(16) DEFAULT NULL,
    `ip_or_terminal` VARCHAR(32) DEFAULT 'LOCAL_TERM',
    `details` VARCHAR(256) DEFAULT NULL,
    INDEX `idx_audit_action` (`action`),
    INDEX `idx_audit_time` (`timestamp`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
