CREATE OR REPLACE VIEW `auctioneer_baseline_item_view` AS
SELECT i.faction, i.item_id, t.name AS item_name, t.Quality, t.class,
       i.suffix, i.price_median, i.price_low, i.price_high,
       i.price_samples, i.listing_typical,
       i.bid_ratio_median, i.bid_ratio_reliable, i.bid_ratio_class_prior_bp
FROM `auctioneer_baseline_item` i
LEFT JOIN `acore_world`.`item_template` t ON t.entry = i.item_id;
