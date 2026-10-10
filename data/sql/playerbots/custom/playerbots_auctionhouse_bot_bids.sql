CREATE TABLE `auctionhouse_bot_bids` (
  `bot_guid`         INT UNSIGNED NOT NULL,
  `auction_id`       INT UNSIGNED NOT NULL,
  `item_template_id` INT UNSIGNED NOT NULL,
  `item_count`       INT UNSIGNED NOT NULL,
  `max_willing`      INT UNSIGNED NOT NULL,
  `tracked_since`    TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`bot_guid`, `auction_id`),
  KEY `idx_bot` (`bot_guid`),
  KEY `idx_auction` (`auction_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
