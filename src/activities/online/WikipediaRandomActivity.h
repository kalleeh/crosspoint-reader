#pragma once
#include <functional>
#include <vector>
#include <map>
#include "../Activity.h"

struct WikiArticle {
  String title;
  String extract;
  String imageUrl;  // Wikipedia image URL
  String cachedImagePath;  // Local cached BMP
  std::vector<String> categories;
  unsigned long viewTime = 0;
  int scrollDepth = 0;
  bool imageDownloadFailed = false;
  int startY = 0;  // Y position where article starts
  int endY = 0;    // Y position where article ends
};

class WikipediaRandomActivity final : public Activity {
  enum State { LOADING, LOADED, ERROR };
  
  // Sliding window: keep at most this many articles in memory.
  // Old articles are evicted from the front as new ones are appended.
  static constexpr int MAX_FEED_SIZE = 20;

  const std::function<void()> onBack;
  State state = LOADED;  // Start in LOADED state
  bool isFetching = false;  // Loading indicator
  int pendingFetches = 0;
  std::vector<WikiArticle> feed;
  int scrollOffset = 0;  // Vertical scroll position
  int maxScroll = 0;
  unsigned long sessionStartTime = 0;
  unsigned long lastFetchTime = 0;  // Prevent rapid fetches
  std::map<String, float> interests;  // keyword -> weight

  void fetchSingleArticle();
  bool downloadNextVisibleImage();
  void fetchNextArticles();
  void evictOldArticles();
  bool downloadAndCacheImage(WikiArticle& article);
  void loadInterests();
  void saveInterests();
  void updateInterests(const WikiArticle& article, int scrollDepth);
  void render();
  
 public:
  explicit WikipediaRandomActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                   const std::function<void()>& onBack)
      : Activity("Wikipedia Feed", renderer, mappedInput), onBack(onBack) {}
  
  void onEnter() override;
  void onExit() override;
  void loop() override;
};
