from pathlib import Path
import re, zipfile
root=Path('/mnt/data/repair_mixed_work')
manager=(root/'Source/GE_AIReplication/SharpMovement/Experiment/SharpExperimentManager.cpp').read_text()
course=(root/'Source/GE_AIReplication/SharpMovement/Experiment/SharpExperimentManagerIntegratedCourse.cpp').read_text()
checks={
 'floor-safe inner-grid candidates': 'CandidateOffsets.Emplace(' in course and 'Columns - 2' in course and 'Rows - 2' in course,
 'deterministic placement': 'FRandomStream LayoutStream(47293)' in course,
 'missing-floor candidates skipped': '++MissingFloor;' in course,
 'overlap check': 'OverlapBlockingTestByChannel' in course,
 'count reporting': 'MissingFloor=%d Blocked=%d OutsideArena=%d' in course,
 'obstacle shortage nonfatal': 'Batch will CONTINUE' in course and 'return true;' in course,
 'batch not coupled to spawn failure': '|| !SpawnIntegratedCourseObstacles(AssignReason)' not in manager,
 'optional course setup': 'if (!SpawnIntegratedCourseObstacles(AssignReason))' in manager and 'bEnableIntegratedMovementCourse = false;' in manager,
 'all 7 modes preserved': all('ESharpExperimentMode::'+x in manager for x in ('Default','Adaptive','AlwaysHigh','SingleShot','Burst','EventOnly','EventPredictor')),
 'knockback preserved': 'void ASharpExperimentManager::ApplyIntegratedKnockbackPulse()' in course,
}
for name,ok in checks.items(): print(('PASS' if ok else 'FAIL')+': '+name)
assert all(checks.values())
for f in [root/'Source/GE_AIReplication/SharpMovement/Experiment/SharpExperimentManager.cpp',root/'Source/GE_AIReplication/SharpMovement/Experiment/SharpExperimentManagerIntegratedCourse.cpp']:
    s=f.read_text(); s=re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"','',s,flags=re.S)
    for a,b in [('{','}'),('(',')')]:
        depth=0
        for c in s:
            if c==a: depth+=1
            if c==b: depth-=1
            assert depth>=0,(f.name,'extra closing',b)
        assert depth==0,(f.name,'unbalanced',a)
print('PASS: basic C++ delimiter balance')
# For the normal 64-enemy auto-spawn, candidates are well inside the old 5,500cm ring.
n=64;cols=8;rows=8;spacing=1000
candidates=[((c-(cols-2)/2)*spacing,(r-(rows-2)/2)*spacing) for r in range(rows-1) for c in range(cols-1)]
assert len(candidates)==49 and max(max(abs(x),abs(y)) for x,y in candidates)==3000
print('PASS: 64 enemy grid -> 49 inner candidate positions with max |XY| = 3000cm')
