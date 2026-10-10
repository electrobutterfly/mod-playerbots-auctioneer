ALTER TABLE `auctioneer_baseline_item`
  ADD COLUMN `trend_bp` INT NOT NULL DEFAULT 0
  COMMENT 'price trend in basis points vs prior snapshot'
  AFTER `bid_ratio_class_prior_bp`;
