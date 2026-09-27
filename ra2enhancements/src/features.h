#pragma once

enum { WWKEY_RELEASE = 0x800 };

void InstallCampaignSpeed();
void InstallQueueBatch();
void InstallSelectVoice();
void InstallHealerEscort();
void InstallRangedGuard();
void InstallQuickSave();
void InstallMissionSaveName();
void InstallCommands();
void InstallAttackMove();

void StepSpeed(int dir);
bool SinglePlayerMode();

bool CommandsReady();
const char *KeyboardIni();

void QueueQuickSave();
void QueueQuickLoad();

void ForgetEscorts();

void ArmAttackMove();
