#include "FWCore/Framework/interface/global/EDProducer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"

#include "FWCore/Utilities/interface/InputTag.h"

#include "DataFormats/SiPixelCluster/interface/SiPixelCluster.h"
#include "DataFormats/Phase2TrackerCluster/interface/Phase2TrackerCluster1D.h"

#include "DataFormats/Common/interface/ValueMap.h"
#include "DataFormats/Common/interface/DetSetVectorNew.h"
#include "DataFormats/Provenance/interface/ProductID.h"
#include "DataFormats/Common/interface/ContainerMask.h"

#include "DataFormats/DetId/interface/DetId.h"

#include "DataFormats/TrackReco/interface/Track.h"
#include "DataFormats/TrackerRecHit2D/interface/ClusterRemovalInfo.h"

#include "TrackingTools/PatternTools/interface/TrackCollectionTokens.h"

#include<limits>

/* This is a copy of the TrackClusterRemover 
 * for Phase2 Tk upgrade
 * FIXME:: changing with new phase2 pixel DataFormats!
 * FIXME:: still to be factorized
 */

namespace {

  class TrackClusterRemoverPhase2 final : public edm::global::EDProducer<> {
  public:
    TrackClusterRemoverPhase2(const edm::ParameterSet& iConfig) ;
    ~TrackClusterRemoverPhase2(){}
    static void fillDescriptions(edm::ConfigurationDescriptions& descriptions);
  private:
    
    virtual void produce(edm::StreamID, edm::Event& evt, const edm::EventSetup&) const override;
 
    using PixelMaskContainer    = edm::ContainerMask<edmNew::DetSetVector<SiPixelCluster>>;
    using Phase2OTMaskContainer = edm::ContainerMask<edmNew::DetSetVector<Phase2TrackerCluster1D>>;

    using QualityMaskCollection = std::vector<unsigned char>;

    const float maxChi2_;
    const int minNumberOfLayersWithMeasBeforeFiltering_;
    const reco::TrackBase::TrackQuality trackQuality_;


    const TrackCollectionTokens trajectories_;
    edm::EDGetTokenT<QualityMaskCollection> srcQuals;
    
    const edm::EDGetTokenT<edmNew::DetSetVector<SiPixelCluster> > pixelClusters_;
    const edm::EDGetTokenT<edmNew::DetSetVector<Phase2TrackerCluster1D> > phase2OTClusters_;
    
    edm::EDGetTokenT<PixelMaskContainer> oldPxlMaskToken_;
    edm::EDGetTokenT<Phase2OTMaskContainer> oldPh2OTMaskToken_;
    
    // backward compatibility during transition period
    edm::EDGetTokenT<edm::ValueMap<int>>  overrideTrkQuals_;
    
  };

  void TrackClusterRemoverPhase2::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("trajectories",edm::InputTag());
    desc.add<edm::InputTag>("trackClassifier",edm::InputTag("","QualityMasks"));
    desc.add<edm::InputTag>("phase2pixelClusters",edm::InputTag("siPixelClusters"));
    desc.add<edm::InputTag>("phase2OTClusters",edm::InputTag("siPhase2Clusters"));
    desc.add<edm::InputTag>("oldClusterRemovalInfo",edm::InputTag());

    desc.add<std::string>("TrackQuality","highPurity");
    desc.add<double>("maxChi2",30.);
    desc.add<int>("minNumberOfLayersWithMeasBeforeFiltering",0);
    // old mode     
    desc.add<edm::InputTag>("overrideTrkQuals",edm::InputTag());

    descriptions.add("phase2trackClusterRemover", desc);
 
  }
  
  TrackClusterRemoverPhase2::TrackClusterRemoverPhase2(const edm::ParameterSet& iConfig) :
    maxChi2_(iConfig.getParameter<double>("maxChi2")),
    minNumberOfLayersWithMeasBeforeFiltering_(iConfig.getParameter<int>("minNumberOfLayersWithMeasBeforeFiltering")),
    trackQuality_(reco::TrackBase::qualityByName(iConfig.getParameter<std::string>("TrackQuality"))),

    trajectories_(iConfig.getParameter<edm::InputTag>("trajectories"),consumesCollector()),
    pixelClusters_(consumes<edmNew::DetSetVector<SiPixelCluster> >(iConfig.getParameter<edm::InputTag>("phase2pixelClusters"))),
    phase2OTClusters_(consumes<edmNew::DetSetVector<Phase2TrackerCluster1D> >(iConfig.getParameter<edm::InputTag>("phase2OTClusters")))
  {

    produces<edm::ContainerMask<edmNew::DetSetVector<SiPixelCluster> > >();
    produces<edm::ContainerMask<edmNew::DetSetVector<Phase2TrackerCluster1D> > >();

    // old mode
    auto const & overrideTrkQuals = iConfig.getParameter<edm::InputTag>("overrideTrkQuals");
    if ( !overrideTrkQuals.label().empty() )
      overrideTrkQuals_ = consumes<edm::ValueMap<int> >(overrideTrkQuals);

    auto const & classifier = iConfig.getParameter<edm::InputTag>("trackClassifier");
    if ( !classifier.label().empty())
      srcQuals = consumes<QualityMaskCollection>(classifier);

    auto const &  oldClusterRemovalInfo = iConfig.getParameter<edm::InputTag>("oldClusterRemovalInfo");
    if (!oldClusterRemovalInfo.label().empty()) {
      oldPxlMaskToken_ = consumes<PixelMaskContainer>(oldClusterRemovalInfo);
      oldPh2OTMaskToken_ = consumes<Phase2OTMaskContainer>(oldClusterRemovalInfo);
    }

  }


  void
  TrackClusterRemoverPhase2::produce(edm::StreamID, edm::Event& iEvent, const edm::EventSetup&) const
  {

 
    edm::Handle<edmNew::DetSetVector<SiPixelCluster> > pixelClusters;
    iEvent.getByToken(pixelClusters_, pixelClusters);
    edm::Handle<edmNew::DetSetVector<Phase2TrackerCluster1D> > phase2OTClusters;
    iEvent.getByToken(phase2OTClusters_, phase2OTClusters);


    std::vector<bool> collectedPixels;
    std::vector<bool> collectedPhase2OTs;

    if(!oldPxlMaskToken_.isUninitialized()) {

      edm::Handle<PixelMaskContainer> oldPxlMask;
      edm::Handle<Phase2OTMaskContainer> oldPh2OTMask;
      iEvent.getByToken(oldPxlMaskToken_ ,oldPxlMask);
      iEvent.getByToken(oldPh2OTMaskToken_ ,oldPh2OTMask);
      LogDebug("TrackClusterRemoverPhase2")<<"to merge in, "<<oldPxlMask->size()<<" phase2 pixel and " << oldPh2OTMask->size() << " phase2 OT";
      oldPxlMask->copyMaskTo(collectedPixels);
      oldPh2OTMask->copyMaskTo(collectedPhase2OTs);
      assert(phase2OTClusters->dataSize() >= collectedPhase2OTs.size());
      collectedPhase2OTs.resize(phase2OTClusters->dataSize(), false);

    }else {

      collectedPixels.resize(pixelClusters->dataSize(), false);
      collectedPhase2OTs.resize(phase2OTClusters->dataSize(), false);

    } 


    // loop over trajectories, filter, mask clusters../

   unsigned char qualMask = ~0;
   if (trackQuality_!=reco::TrackBase::undefQuality) qualMask = 1<<trackQuality_; 
 
    
    auto const & tracks = trajectories_.tracks(iEvent);
    auto const & trajs = trajectories_.trajectories(iEvent);
    auto s = tracks.size();

    assert(s==trajs.size());

    QualityMaskCollection oldStyle;
    QualityMaskCollection const * pquals=nullptr;
    
    if (!overrideTrkQuals_.isUninitialized()) {
      edm::Handle<edm::ValueMap<int> > quals;
      iEvent.getByToken(overrideTrkQuals_,quals);
      assert(s==(*quals).size());

      oldStyle.resize(s,0);
      for (auto i=0U; i<s; ++i) if ( (*quals).get(i) > 0 ) oldStyle[i] = (255)&(*quals).get(i);
      pquals = &oldStyle; 
    }

    if (!srcQuals.isUninitialized()) {
      edm::Handle<QualityMaskCollection> hqual;
      iEvent.getByToken(srcQuals, hqual);
      pquals = hqual.product();
    }

    for (auto i=0U; i<s; ++i){

      const reco::Track & track = tracks[i];
      bool goodTk =  (pquals) ? (*pquals)[i] & qualMask : track.quality(trackQuality_);
      if ( !goodTk) continue;
      if(track.hitPattern().trackerLayersWithMeasurement() < minNumberOfLayersWithMeasBeforeFiltering_) continue;
      const Trajectory &tj = trajs[i];
      const auto & tms = tj.measurements();
      for (auto const & tm :  tms) {
	auto const & hit = *tm.recHit();
	if (!hit.isValid()) continue; 
	if ( tm.estimate() > maxChi2_ ) continue; // skip outliers
        auto const & thit = reinterpret_cast<BaseTrackerRecHit const&>(hit);
        auto const & cluster = thit.firstClusterRef();
        // FIXME when we will get also Phase2 pixel
	if (cluster.isPixel()) collectedPixels[cluster.key()]=true;
	else if (cluster.isPhase2()) collectedPhase2OTs[cluster.key()]=true;
      }

    }


    auto removedPixelClusterMask= 
      std::make_unique<PixelMaskContainer>(edm::RefProd<edmNew::DetSetVector<SiPixelCluster>>(pixelClusters),collectedPixels);      
    LogDebug("TrackClusterRemoverPhase2")<<"total pxl to skip: "<<std::count(collectedPixels.begin(),collectedPixels.end(),true);
    iEvent.put(std::move(removedPixelClusterMask));
 
    auto removedPhase2OTClusterMask= 
      std::make_unique<Phase2OTMaskContainer>(edm::RefProd<edmNew::DetSetVector<Phase2TrackerCluster1D>>(phase2OTClusters),collectedPhase2OTs);      
    LogDebug("TrackClusterRemoverPhase2")<<"total ph2OT to skip: "<<std::count(collectedPhase2OTs.begin(),collectedPhase2OTs.end(),true);
    iEvent.put(std::move(removedPhase2OTClusterMask));


  }


}


#include "FWCore/PluginManager/interface/ModuleDef.h"
#include "FWCore/Framework/interface/MakerMacros.h"
DEFINE_FWK_MODULE(TrackClusterRemoverPhase2);
