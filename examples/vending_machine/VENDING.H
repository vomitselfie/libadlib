//	vending.h:	the ids the vending-machine plans refer to (libadlib example).
//
//	Like the 1996 compiler's enumIDs.h, this file should contain nothing but enum statements and
//	comments: libadlib's Vocabulary::loadEnumHeader reads it at start-up, so ids are positions.
//	The enum names are the ones libadlib binds its roles to by default (see docs/host-api.md).

enum ID_MOD { id_MOD_Machine, id_MOD_Customer, id_MOD_NOMODEL };

enum ID_MSG {
	id_MSG_Coin, id_MSG_Select, id_MSG_Refund, id_MSG_VendDone, id_MSG_Dispensed,
	id_MSG_Initiate,		// runtime-reserved: queued to self when a behaviour is entered
	id_MSG_ExitBehavior,	// runtime-reserved: sent to self before a behaviour change
	id_MSG_NOMSG };			// runtime-reserved: "no message"; also the table size

enum Behavior_IDs { id_BEH_Machine, id_BEH_Person, id_BEH_NUMBEHAVIORS };

enum AgendaItemIDs { id_AGD_CTimerActivity, id_AGD_NUMAGDITEMS };

enum ActionFunctionIDs {
	id_ACF_AddCredit, id_ACF_Display, id_ACF_Dispense, id_ACF_ReturnChange, id_ACF_SendMessage,
	id_ACF_Say,
	id_ACF_NUMACTIONFUNCTIONS };

enum DecisionFunctionIDs { id_DCF_EnoughCredit, id_DCF_InStock, id_DCF_NUMDECISIONFUNCTIONS };

// Framework parameters are found by name, so their positions are free.
enum BehaviorSetParamFnIDs { id_SET_InteractorLatency, id_SET_Digressive, id_SET_PurgeFlag, id_SET_NUMBEHSETFNS };
enum InteractorSetParamFnIDs { id_SET_InteractorName, id_SET_ActiveState, id_SET_NUMINTSETFNS };

enum Pronouns {
	id_PRN_Me,			// load-time: the owner's object index
	id_PRN_Customer,	// run-time: whoever is at the machine
	id_PRN_NUMPRONOUNS };
