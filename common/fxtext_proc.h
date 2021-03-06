
// Returns a pointer to AN ARRAY of vectors (one vector for each realline), not a pointer to a vector (as one would think).
//extern LANGSPEC vector_t *FXTEXTS_get(const struct WBlocks *wblock, const struct WTracks *wtrack, const struct FXs *fxs);

#if USE_QT4
const FXText_trss FXTEXTS_get(const struct WBlocks *wblock, const struct WTracks *wtrack, const struct FXs *fxs);
#endif

extern LANGSPEC int FXTEXT_subsubtrack(const struct Tracker_Windows *window, const struct WTracks *wtrack, struct FXs **to_fxs);
extern LANGSPEC bool FXTEXT_keypress(struct Tracker_Windows *window, struct WBlocks *wblock, struct WTracks *wtrack, int realline, const Place *place, int key);

static inline int FXTEXT_num(struct Tracks *track){
  int ret = 0;

#if __cplusplus  
  VECTOR_FOR_EACH(const struct FXs *, fxs, &track->fxs)
#else
  VECTOR_FOR_EACH(const struct FXs *fxs, &track->fxs)
#endif
    {
      if(fxs->fx->is_enabled)
        ret++;
    }END_VECTOR_FOR_EACH;
  
  return ret;
}

static inline bool FXTEXT_has(struct Tracks *track){
#if __cplusplus  
  VECTOR_FOR_EACH(const struct FXs *, fxs, &track->fxs)
#else
  VECTOR_FOR_EACH(const struct FXs *fxs, &track->fxs)
#endif
    {
      if(fxs->fx->is_enabled)
        return true;
    }END_VECTOR_FOR_EACH;
  
  return false;
}
