#ifndef DataCollection_h
#define DataCollection_h


#include <map>
#include <list>
#include <string>
#include "DebugReporter.h"
#include "IDTracker.h"
#include "TclCommandManager.h"

class DataCollection : public DebugReporter, public IDTracker<DataCollection>, public TclCommandListener {

 public:

  DataCollection();
  virtual ~DataCollection(); 

  // Used to poll for any displayable data at the given point.
  virtual void GetInfoAtRAS( float const iX, float const iY, float const iZ,
			     std::map<std::string,std::string>& iLabelValues );

  // Should return a type description unique to the subclass.
  virtual std::string GetTypeDescription() { return "BaseCollection"; }

  std::string GetLabel() const { return msLabel; }
  void SetLabel( std::string const isLabel ) { msLabel = isLabel; }
  
  virtual void DoListenToTclCommand ( char* isCommand, int iArgc, char** iasArgv );
protected:
  std::string msLabel;

};



#endif
