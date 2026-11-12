"""Verify the modified archive preserves original user files and C++ additions."""
from zipfile import ZipFile
from pathlib import Path
from hashlib import sha256
import sys

original=Path(sys.argv[1]); revised=Path(sys.argv[2])
assert original.exists() and revised.exists()
with ZipFile(original) as src, ZipFile(revised) as dst:
    assert src.testzip() is None, 'Bad original ZIP member'
    assert dst.testzip() is None, 'Bad revised ZIP member'
    old=set(src.namelist()); new=set(dst.namelist())
    assert not (old-new), f"Original entries removed: {sorted(old-new)[:8]}"
    content=[n for n in old if n.endswith(('.uasset','.umap'))]
    for name in content:
        assert src.read(name)==dst.read(name), f"Binary asset modified: {name}"
    assert b'RandomWalk' in dst.read('Source/GE_AIReplication/SharpMovement/Enemy/SharpEnemyCharacter.h')
    assert b'FRandomStream' in dst.read('Source/GE_AIReplication/SharpMovement/Movement/SharpRandomWalkerComponent.h')
    assert b'300.0f' in dst.read('Source/GE_AIReplication/SharpMovement/Movement/SharpRandomWalkTypes.h')
    print('Package verified:')
    print(' - original entries:',len(old),'revised entries:',len(new),'additional:',len(new-old))
    print(' - preserved binary assets:',len(content))
    print(' - ZIP CRC passes for all members')
    print(' - bytes revised:',revised.stat().st_size,'sha256:',sha256(revised.read_bytes()).hexdigest())
