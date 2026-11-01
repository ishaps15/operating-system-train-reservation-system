-- ============================================================================
-- Operating System-Based Train Ticket Reservation System
-- Seed Dataset: Eastern Railway / Durgapur Junction (DGR) Hub
-- ============================================================================

USE `train_reservation_db`;

-- ----------------------------------------------------------------------------
-- 1. Stations
-- ----------------------------------------------------------------------------
INSERT INTO `stations` (`station_code`, `station_name`, `division`, `zone`, `platform_count`) VALUES
('HWH',  'Howrah Junction',          'Howrah',     'Eastern Railway (ER)',   23),
('BDC',  'Bandel Junction',          'Howrah',     'Eastern Railway (ER)',   5),
('BWN',  'Barddhaman Junction',      'Howrah',     'Eastern Railway (ER)',   8),
('DGR',  'Durgapur',                 'Asansol',    'Eastern Railway (ER)',   5),
('ASN',  'Asansol Junction',         'Asansol',    'Eastern Railway (ER)',   8),
('DHN',  'Dhanbad Junction',         'Dhanbad',    'East Central Rly (ECR)', 8),
('GAYA', 'Gaya Junction',            'Pt DDU',     'East Central Rly (ECR)', 9),
('DDU',  'Pt. Deen Dayal Upadhyaya', 'Pt DDU',     'East Central Rly (ECR)', 8),
('PRYJ', 'Prayagraj Junction',       'Prayagraj',  'North Central Rly (NCR)',10),
('CNB',  'Kanpur Central',           'Kanpur',     'North Central Rly (NCR)',10),
('NDLS', 'New Delhi',                'Delhi',      'Northern Railway (NR)',  16),
('RNC',  'Ranchi Junction',          'Ranchi',     'South Eastern Rly (SER)', 6),
('NJP',  'New Jalpaiguri',           'Katihar',    'Northeast Frontier (NFR)',5),
('DDN',  'Dehradun',                 'Moradabad',  'Northern Railway (NR)',   4)
ON DUPLICATE KEY UPDATE `station_name` = VALUES(`station_name`);

-- ----------------------------------------------------------------------------
-- 2. Initial Users
-- Password for all seed users is formatted as sha256(salt + "$" + raw_password)
-- admin: Admin@123 | dgr_counter1: Counter@123 | hwh_counter1: Counter@123 | passenger1: Pass@123 | sourav: Sourav@123 | tte_dgr: Tte@123
-- ----------------------------------------------------------------------------
INSERT INTO `users` (`user_id`, `username`, `password_hash`, `salt`, `full_name`, `role`, `phone`, `email`, `station_code`) VALUES
(1, 'admin',        'e8432a5ff50b8686f03fa65239a0397aa2104526b7ee69b9148d47b665fa9280', 's4lt_dgr_admin_001', 'Chief Commercial Manager', 'ADMINISTRATOR', '+91-343-255011', 'ccm@er.railnet.gov.in', 'DGR'),
(2, 'dgr_counter1', '74906f343a0eeb960301a24d78be464d509f6bf79e2ba6d3f23a67d0ceae1c0a', 's4lt_dgr_agent_001', 'Durgapur PRS Counter Clerk 1', 'BOOKING_AGENT', '+91-9832100111', 'dgr_prs1@er.railnet.gov.in', 'DGR'),
(3, 'hwh_counter1', '74906f343a0eeb960301a24d78be464d509f6bf79e2ba6d3f23a67d0ceae1c0a', 's4lt_hwh_agent_002', 'Howrah PRS Counter Clerk 1', 'BOOKING_AGENT', '+91-9832200222', 'hwh_prs1@er.railnet.gov.in', 'HWH'),
(4, 'passenger1',   '896aeb3bc2a3f7ff9e2236316fa71ce9fc1cf367d3e404b93ebf18c20531c19b', 's4lt_pass_001',      'Amitava Mukherjee',           'PASSENGER',     '+91-9876543210', 'amitava.m@gmail.com', 'DGR'),
(5, 'sourav',       'd8417d7b539c2794eb0b8aa7519ea8aa8448ebfcbb6b7e32d3b4a4cb5c8a6ebf', 's4lt_pass_002',      'Sourav Ganguly',              'PASSENGER',     '+91-9830012345', 'sourav.g@outlook.com', 'HWH'),
(6, 'tte_dgr',      'e11c210515234190bce29b13200ff01b69cf33383a15c3ec296f0227918ba046', 's4lt_tte_001',       'Rajesh Sharma (TTE)',         'TTE_CONDUCTOR', '+91-9434056789', 'tte.dgr@er.railnet.gov.in', 'DGR')
ON DUPLICATE KEY UPDATE `full_name` = VALUES(`full_name`);

-- ----------------------------------------------------------------------------
-- 3. Trains passing through Durgapur / Eastern Railway Corridor
-- ----------------------------------------------------------------------------
INSERT INTO `trains` (`train_no`, `train_name`, `source_code`, `dest_code`, `total_distance_km`, `departure_time`, `arrival_time`, `is_superfast`, `base_rate_per_km`, `capacity_1a`, `capacity_2a`, `capacity_3a`, `capacity_cc`, `capacity_sl`, `capacity_2s`, `rac_limit`, `wl_limit`) VALUES
(12301, 'Howrah - New Delhi Rajdhani Express', 'HWH', 'NDLS', 1447, '16:50', '10:05', TRUE,  2.80, 24, 48, 64,  0,  0,  0, 10, 20),
(12302, 'New Delhi - Howrah Rajdhani Express', 'NDLS', 'HWH', 1447, '16:55', '09:55', TRUE,  2.80, 24, 48, 64,  0,  0,  0, 10, 20),
(12303, 'Poorva Express (via Patna/Durgapur)', 'HWH', 'NDLS', 1530, '08:00', '06:05', TRUE,  0.75, 18, 36, 64,  0, 72, 72, 20, 40),
(12019, 'Howrah - Ranchi Shatabdi Express',    'HWH', 'RNC',   412, '06:05', '13:15', TRUE,  1.60,  0,  0,  0, 72,  0, 72, 10, 20),
(13009, 'Doon Express (Howrah - Dehradun)',   'HWH', 'DDN',  1530, '20:25', '07:35', FALSE, 0.60,  0, 24, 64,  0, 72, 72, 25, 50),
(22301, 'Howrah - NJP Vande Bharat Express',   'HWH', 'NJP',   560, '05:55', '13:25', TRUE,  2.20,  0,  0,  0, 72,  0,  0,  8, 16)
ON DUPLICATE KEY UPDATE `train_name` = VALUES(`train_name`);

-- ----------------------------------------------------------------------------
-- 4. Route Stops for Key Trains (12301 Rajdhani & 12303 Poorva)
-- ----------------------------------------------------------------------------
INSERT INTO `route_stops` (`train_no`, `station_code`, `stop_sequence`, `distance_from_source_km`, `arr_time`, `dep_time`, `halt_minutes`) VALUES
(12301, 'HWH',  1,    0, 'SRC',   '16:50', 0),
(12301, 'BWN',  2,  107, '17:58', '18:00', 2),
(12301, 'DGR',  3,  171, '18:41', '18:43', 2),
(12301, 'ASN',  4,  213, '19:10', '19:12', 2),
(12301, 'DHN',  5,  272, '19:55', '20:00', 5),
(12301, 'GAYA', 6,  471, '22:19', '22:22', 3),
(12301, 'DDU',  7,  676, '00:45', '00:55', 10),
(12301, 'PRYJ', 8,  829, '02:33', '02:35', 2),
(12301, 'CNB',  9, 1023, '04:40', '04:45', 5),
(12301, 'NDLS', 10, 1447, '10:05', 'DST',   0),

(12019, 'HWH',  1,    0, 'SRC',   '06:05', 0),
(12019, 'DGR',  2,  171, '07:44', '07:46', 2),
(12019, 'ASN',  3,  213, '08:22', '08:24', 2),
(12019, 'DHN',  4,  272, '09:20', '09:25', 5),
(12019, 'RNC',  5,  412, '13:15', 'DST',   0)
ON DUPLICATE KEY UPDATE `arr_time` = VALUES(`arr_time`);

-- ----------------------------------------------------------------------------
-- 5. Seed Bookings and PNRs
-- ----------------------------------------------------------------------------
INSERT INTO `bookings` (`booking_id`, `pnr`, `user_id`, `train_no`, `source_code`, `dest_code`, `travel_date`, `travel_class`, `quota`, `passenger_count`, `base_fare`, `surge_amount`, `tatkal_charge`, `discount_amount`, `gst_amount`, `total_fare`, `overall_status`) VALUES
(1, '234-8910231', 4, 12301, 'DGR', 'NDLS', '2026-11-15', '3A', 'GENERAL', 2, 3800.00, 380.00, 0.00, 0.00, 209.00, 4389.00, 'CNF'),
(2, '234-8910232', 5, 12019, 'DGR', 'RNC',  '2026-11-16', 'CC', 'GENERAL', 1,  550.00,   0.00, 0.00, 0.00,  27.50,  577.50, 'CNF')
ON DUPLICATE KEY UPDATE `total_fare` = VALUES(`total_fare`);

INSERT INTO `passengers` (`passenger_id`, `booking_id`, `pnr`, `name`, `age`, `gender`, `preferred_berth`, `allocated_berth`, `coach_code`, `seat_number`, `status`, `concession`, `individual_fare`) VALUES
(1, 1, '234-8910231', 'Amitava Mukherjee', 38, 'M', 'LOWER', 'LOWER', 'B1', 21, 'CNF', 'NONE', 2194.50),
(2, 1, '234-8910231', 'Sunita Mukherjee',  35, 'F', 'UPPER', 'UPPER', 'B1', 23, 'CNF', 'NONE', 2194.50),
(3, 2, '234-8910232', 'Sourav Ganguly',    52, 'M', 'WINDOW', 'WINDOW', 'C1', 15, 'CNF', 'NONE', 577.50)
ON DUPLICATE KEY UPDATE `name` = VALUES(`name`);

-- ----------------------------------------------------------------------------
-- 6. Initial Audit Log
-- ----------------------------------------------------------------------------
INSERT INTO `audit_logs` (`user_id`, `action`, `pnr`, `ip_or_terminal`, `details`) VALUES
(1, 'SYSTEM_INIT', NULL, 'SYSTEM', 'Initialized Eastern Railway Durgapur Division Relational Database Schema.');
