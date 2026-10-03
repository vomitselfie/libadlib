// enumIDs.h: the test vocabulary of the ADLIB compiler compatibility corpus (fixtures/compiler).
//
// The 1996 compiler reads this file first: every token between braces is a symbol whose value is
// its position in its enum. The enum names bind libadlib roles (ID_MSG, ActionFunctionIDs, ...).
// The fixtures were frozen by running the original 1996 compiler with these four files as its
// symbol files, so their positions are part of the expectations: append, do not reorder.

enum ID_MSG {
	id_MSG_Ping, id_MSG_Pong, id_MSG_BlinkBellKe, id_MSG_CountShakeBeepSpin,
	id_MSG_FallFallJu, id_MSG_Hop, id_MSG_HumSinkBe, id_MSG_JumpOpenDoo,
	id_MSG_PushCountClo, id_MSG_StepBellRin, id_MSG_StepCl, id_MSG_StoreJump,
	id_MSG_TurnSignalTickCoinFloa, id_MSG_Initiate, id_MSG_ExitBehavior, id_MSG_NOMSG };

enum CollisionObjectID {
	id_COB_NodBeepSi, id_COB_PlayerPart_0, id_COB_PlayerPart_1, id_COB_PlayerPart_2,
	id_COB_NUMCOB };

enum Behavior_IDs {
	id_BEH_CardStopStor, id_BEH_ClapWavePullFl, id_BEH_FallFallLampBee, id_BEH_JumpTickJumpH,
	id_BEH_Res, id_BEH_Signa, id_BEH_SignalCoi, id_BEH_StandStill,
	id_BEH_WaveBlin, id_BEH_NUMBEHAVIORS };

enum AgendaItemIDs {
	id_AGD_RestLampStepG, id_AGD_RingBoxTurnR, id_AGD_CTimerActivity, id_AGD_NUMAGDITEMS };

enum ActionFunctionIDs {
	id_ACF_BallHopCloseSin, id_ACF_BeepLowerDoo, id_ACF_BoxCoinFetchTurnS, id_ACF_CardRestRingSpin,
	id_ACF_FetchKeyHum, id_ACF_LowerStopHumS, id_ACF_PlayTone, id_ACF_Probe,
	id_ACF_PullCloseLookTu, id_ACF_PushBlinkRingS, id_ACF_RaiseD, id_ACF_RaiseRaiseHumClapRe,
	id_ACF_ReadyOpenHopDoorS, id_ACF_RiseGlowPullWav, id_ACF_SendMessage, id_ACF_SetPose,
	id_ACF_ShowText, id_ACF_SingGateCoi, id_ACF_StepH, id_ACF_TurnLook,
	id_ACF_TurnRingRunS, id_ACF_WatchObject, id_ACF_NUMACTIONFUNCTIONS };

enum DecisionFunctionIDs {
	id_DCF_BeepCountCloseRe, id_DCF_BoxKeyLightLower, id_DCF_Choose, id_DCF_FloatRiseShakeOpen,
	id_DCF_GateBlinkOpenCoinW, id_DCF_GlowShakeGlowStepTur, id_DCF_LightHumWalkDashClo, id_DCF_RestDashWaveCo,
	id_DCF_TickBeepFloatGateS, id_DCF_NUMDECISIONFUNCTIONS };

enum BehaviorSetParamFnIDs {
	id_SET_KeyMonitorMode, id_SET_InteractorLatency, id_SET_LampLampRiseBlin, id_SET_Digressive,
	id_SET_PurgeFlag, id_SET_NUMBEHSETFNS };

enum InteractorSetParamFnIDs {
	id_SET_InteractorName, id_SET_ActiveState, id_SET_NUMINTSETFNS };

enum Pronouns {
	id_PRN_Me, id_PRN_RiseLowerC, id_PRN_Viewer, id_PRN_NUMPRONOUNS };

enum DirectionFlag {
	A_AHEAD, A_BACKWRD, A_LEFT, A_RIGHT };

enum TargetState {
	RET_Fix, RET_Lost };
