#include "HitWallReactionLibrary.h"

bool UHitWallReactionLibrary::FindCandidate(FName CandidateId, FHitWallReactionCandidate& Candidate, bool bRequireReviewed) const
{
    Candidate = {};
    for (const auto& Entry : Candidates)
    {
        if (Entry.CandidateId == CandidateId && (!bRequireReviewed || Entry.bReviewed))
        {
            Candidate = Entry;
            return true;
        }
    }
    return false;
}
