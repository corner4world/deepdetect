/**
 * DeepDetect
 * Copyright (c) 2016 Emmanuel Benazera
 * Author: Emmanuel Benazera <beniz@droidnik.fr>
 *
 * This file is part of deepdetect.
 *
 * deepdetect is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * deepdetect is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with deepdetect.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SUPERVISEDOUTPUTCONNECTOR_H
#define SUPERVISEDOUTPUTCONNECTOR_H
#define TS_METRICS_EPSILON 1E-2

#include "dto/output_connector.hpp"

template <typename T>
bool SortScorePairDescend(const std::pair<double, T> &pair1,
                          const std::pair<double, T> &pair2)
{
  return pair1.first > pair2.first;
}
template bool SortScorePairDescend(const std::pair<double, int> &pair1,
                                   const std::pair<double, int> &pair2);
template bool
SortScorePairDescend(const std::pair<double, std::pair<int, int>> &pair1,
                     const std::pair<double, std::pair<int, int>> &pair2);

namespace dd
{

  /**
   * \brief supervised machine learning output connector class
   */
  class SupervisedOutput : public OutputConnectorStrategy
  {
  public:
    /**
     * \brief supervised result
     */
    class sup_result
    {
    public:
      /**
       * \brief constructor
       * @param loss result loss
       */
      sup_result(const std::string &label, const double &loss = 0.0)
          : _label(label), _loss(loss)
      {
      }

      /**
       * \brief destructor
       */
      ~sup_result()
      {
      }

      /**
       * \brief add category to result
       * @param prob category predicted probability
       * @Param cat category name
       */
      inline void add_cat(const double &prob, const std::string &cat)
      {
        _cats.insert(std::pair<double, std::string>(prob, cat));
      }

      inline void add_bbox(const double &prob, const APIData &ad)
      {
        _bboxes.insert(std::pair<double, APIData>(prob, ad));
      }

      inline void add_mask(const double &prob, const APIData &mask)
      {
        _masks.insert(std::pair<double, APIData>(prob, mask));
      }

      inline void add_val(const double &prob, const APIData &ad)
      {
        _vals.insert(std::pair<double, APIData>(prob, ad));
      }

      inline void add_timeseries(const double &prob, const APIData &ad)
      {
        _series.insert(std::pair<double, APIData>(prob, ad));
      }

#ifdef USE_SIMSEARCH
      void add_nn(const double &dist, const URIData &uri)
      {
        _nns.insert(std::pair<double, URIData>(dist, uri));
      }

      void add_bbox_nn(const int &bb, const double &dist, const URIData &uri)
      {
        if (_bbox_nns.empty())
          _bbox_nns = std::vector<std::multimap<double, URIData>>(
              _bboxes.size(), std::multimap<double, URIData>());
        _bbox_nns.at(bb).insert(std::pair<double, URIData>(dist, uri));
      }
#endif

      std::string _label;
      double _loss = 0.0; /**< result loss. */
      std::multimap<double, std::string, std::greater<double>>
          _cats; /**< categories and probabilities for this result */
      std::multimap<double, APIData, std::greater<double>>
          _bboxes; /**< bounding boxes information */
      std::multimap<double, APIData, std::greater<double>>
          _vals; /**< extra data or information added to output, e.g. roi */
      std::multimap<double, APIData, std::greater<double>>
          _series; /** <extra data for timeseries */
      std::multimap<double, APIData, std::greater<double>>
          _masks; /**< masks information */
#ifdef USE_SIMSEARCH
      bool _indexed = false;
      std::multimap<double, URIData> _nns; /**< nearest neigbors. */
      std::vector<std::multimap<double, URIData>>
          _bbox_nns;          /**< per bbox nearest neighbors. */
      std::string _index_uri; /**< alternative URI to store in index in place
                                 of original URI. */
#endif
    };

  public:
    /**
     * \brief supervised output connector constructor
     */
    SupervisedOutput() : OutputConnectorStrategy()
    {
    }

    /**
     * \brief supervised output connector copy-constructor
     * @param sout supervised output connector
     */
    SupervisedOutput(const SupervisedOutput &sout)
        : OutputConnectorStrategy(sout), _best(sout._best)
    {
    }

    ~SupervisedOutput()
    {
    }

    /**
     * \brief supervised output connector initialization
     * @param ad data object for "parameters/output"
     */
    void init(const APIData &ad)
    {
      APIData ad_out = ad.getobj("parameters").getobj("output");
      auto output_params = ad_out.createSharedDTO<DTO::OutputConnector>();
      if (output_params->best != nullptr)
        _best = output_params->best;
    }

    /**
     * \brief add prediction result to supervised connector output
     * @param vrad vector of data objects
     */
    inline void add_results(const std::vector<APIData> &vrad)
    {
      std::unordered_map<std::string, int>::iterator hit;
      for (APIData ad : vrad)
        {
          std::string uri = ad.get("uri").get<std::string>();
          std::string index_uri;
#ifdef USE_SIMSEARCH
          if (ad.has("index_uri"))
            index_uri = ad.get("index_uri").get<std::string>();
#endif
          double loss = ad.get("loss").get<double>();
          std::vector<double> probs
              = ad.get("probs").get<std::vector<double>>();
          std::vector<std::string> cats;
          if (ad.has("cats"))
            cats = ad.get("cats").get<std::vector<std::string>>();
          std::vector<APIData> bboxes;
          std::vector<APIData> rois;
          std::vector<APIData> masks;
          if (ad.has("bboxes"))
            bboxes = ad.getv("bboxes");
          if (ad.has("vals"))
            {
              rois = ad.getv("vals");
            }
          std::vector<APIData> series;
          if (ad.has("series"))
            {
              series = ad.getv("series");
            }

          if (ad.has("masks"))
            masks = ad.getv("masks");
          if ((hit = _vcats.find(uri)) == _vcats.end())
            {
              auto resit = _vcats.insert(
                  std::pair<std::string, int>(uri, _vvcats.size()));
              sup_result supres(uri, loss);

#ifdef USE_SIMSEARCH
              if (!index_uri.empty())
                supres._index_uri = index_uri;
#endif
              _vvcats.push_back(supres);
              hit = resit.first;
              for (size_t i = 0; i < probs.size(); i++)
                {
                  if (!cats.empty())
                    _vvcats.at((*hit).second).add_cat(probs.at(i), cats.at(i));
                  if (!bboxes.empty())
                    _vvcats.at((*hit).second)
                        .add_bbox(probs.at(i), bboxes.at(i));
                  if (!rois.empty())
                    _vvcats.at((*hit).second).add_val(probs.at(i), rois.at(i));
                  if (!series.empty())
                    _vvcats.at((*hit).second)
                        .add_timeseries(probs.at(i), series.at(i));
                  if (!masks.empty())
                    _vvcats.at((*hit).second)
                        .add_mask(probs.at(i), masks.at(i));
                }
            }
        }
    }

    /**
     * \brief best categories selection from results
     * @param ad_out output data object
     * @param bcats supervised output connector
     */
    void best_cats(SupervisedOutput &bcats, const int &output_param_best,
                   const int &nclasses, const bool &has_bbox,
                   const bool &has_roi, const bool &has_mask) const
    {
      int best = output_param_best;
      if (best == -1)
        best = nclasses;
      if (!has_bbox && !has_roi && !has_mask)
        {
          for (size_t i = 0; i < _vvcats.size(); i++)
            {
              sup_result sresult = _vvcats.at(i);
              sup_result bsresult(sresult._label, sresult._loss);
#ifdef USE_SIMSEARCH
              bsresult._index_uri = sresult._index_uri;
#endif
              std::copy_n(
                  sresult._cats.begin(),
                  std::min(best, static_cast<int>(sresult._cats.size())),
                  std::inserter(bsresult._cats, bsresult._cats.end()));
              if (!sresult._bboxes.empty())
                std::copy_n(
                    sresult._bboxes.begin(),
                    std::min(best, static_cast<int>(sresult._bboxes.size())),
                    std::inserter(bsresult._bboxes, bsresult._bboxes.end()));
              if (!sresult._vals.empty())
                std::copy_n(
                    sresult._vals.begin(),
                    std::min(best, static_cast<int>(sresult._vals.size())),
                    std::inserter(bsresult._vals, bsresult._vals.end()));
              if (!sresult._masks.empty())
                std::copy_n(
                    sresult._masks.begin(),
                    std::min(best, static_cast<int>(sresult._masks.size())),
                    std::inserter(bsresult._masks, bsresult._masks.end()));

              bcats._vcats.insert(std::pair<std::string, int>(
                  sresult._label, bcats._vvcats.size()));
              bcats._vvcats.push_back(bsresult);
            }
        }
      else
        {
          for (size_t i = 0; i < _vvcats.size(); i++)
            {
              sup_result sresult = _vvcats.at(i);
              sup_result bsresult(sresult._label, sresult._loss);
#ifdef USE_SIMSEARCH
              bsresult._index_uri = sresult._index_uri;
#endif

              if (best == nclasses)
                {
                  int nbest = sresult._cats.size();
                  std::copy_n(
                      sresult._cats.begin(),
                      std::min(nbest, static_cast<int>(sresult._cats.size())),
                      std::inserter(bsresult._cats, bsresult._cats.end()));
                  if (!sresult._bboxes.empty())
                    std::copy_n(sresult._bboxes.begin(),
                                std::min(nbest, static_cast<int>(
                                                    sresult._bboxes.size())),
                                std::inserter(bsresult._bboxes,
                                              bsresult._bboxes.end()));
                }
              else
                {
                  std::unordered_map<std::string, int> lboxes;
                  auto bbit = lboxes.begin();
                  auto mit = sresult._cats.begin();
                  auto mitx = sresult._bboxes.begin();
                  auto mity = sresult._vals.begin();
                  auto mitmask = sresult._masks.begin();
                  while (mitx != sresult._bboxes.end())
                    {
                      APIData bbad = (*mitx).second;
                      APIData vvad;
                      if (has_roi)
                        vvad = (*mity).second;
                      APIData maskad;
                      if (has_mask)
                        maskad = (*mitmask).second;
                      std::string bbkey
                          = std::to_string(bbad.get("xmin").get<double>())
                            + "-"
                            + std::to_string(bbad.get("ymin").get<double>())
                            + "-"
                            + std::to_string(bbad.get("xmax").get<double>())
                            + "-"
                            + std::to_string(bbad.get("ymax").get<double>());
                      if ((bbit = lboxes.find(bbkey)) != lboxes.end())
                        {
                          (*bbit).second += 1;
                          if ((*bbit).second <= best)
                            {
                              bsresult._cats.insert(
                                  std::pair<double, std::string>(
                                      (*mit).first, (*mit).second));
                              bsresult._bboxes.insert(
                                  std::pair<double, APIData>((*mitx).first,
                                                             bbad));
                              if (has_roi)
                                bsresult._vals.insert(
                                    std::pair<double, APIData>((*mity).first,
                                                               vvad));
                              if (has_mask)
                                bsresult._masks.insert(
                                    std::pair<double, APIData>(
                                        (*mitmask).first, maskad));
                            }
                        }
                      else
                        {
                          lboxes.insert(std::pair<std::string, int>(bbkey, 1));
                          bsresult._cats.insert(std::pair<double, std::string>(
                              (*mit).first, (*mit).second));
                          bsresult._bboxes.insert(
                              std::pair<double, APIData>((*mitx).first, bbad));
                          if (has_roi)
                            bsresult._vals.insert(std::pair<double, APIData>(
                                (*mity).first, vvad));
                          if (has_mask)
                            bsresult._masks.insert(std::pair<double, APIData>(
                                (*mitmask).first, maskad));
                        }
                      ++mitx;
                      ++mit;
                      if (has_roi)
                        ++mity;
                      if (has_mask)
                        ++mitmask;
                    }
                }
              bcats._vcats.insert(std::pair<std::string, int>(
                  sresult._label, bcats._vvcats.size()));
              bcats._vvcats.push_back(bsresult);
            }
        }
    }

#ifdef USE_SIMSEARCH
    double multibox_distance(const double &dist, const double &prob)
    {
      (void)prob; // XXX: probability is unused at the moment
      return dist;
    }
#endif

    /**
     * \brief finalize output supervised connector data
     * @param ad_in data output object from the API call
     * @param ad_out data object as the call response
     */
    void finalize(const APIData &ad_in, APIData &ad_out, MLModel *mlm)
    {
      auto output_params = ad_in.createSharedDTO<DTO::OutputConnector>();
      finalize(output_params, ad_out, mlm);
    }

    /**
     * \brief finalize output supervised connector data
     * @param ad_in data output object from the API call
     * @param ad_out data object as the call response
     */
    void finalize(oatpp::Object<DTO::OutputConnector> output_params,
                  APIData &ad_out, MLModel *mlm)
    {
#ifndef USE_SIMSEARCH
      (void)mlm;
#endif
      SupervisedOutput bcats(*this);
      bool regression = false;
      bool autoencoder = false;
      int nclasses = -1;
      if (ad_out.has("nclasses"))
        nclasses = ad_out.get("nclasses").get<int>();
      if (ad_out.has("regression"))
        {
          if (ad_out.get("regression").get<bool>())
            {
              regression = true;
              _best = ad_out.get("nclasses").get<int>();
            }
          ad_out.erase("regression");
          ad_out.erase("nclasses");
        }
      if (ad_out.has("autoencoder") && ad_out.get("autoencoder").get<bool>())
        {
          autoencoder = true;
          _best = 1;
          ad_out.erase("autoencoder");
        }

      bool has_bbox = ad_out.has("bbox") && ad_out.get("bbox").get<bool>();
      bool has_roi = ad_out.has("roi") && ad_out.get("roi").get<bool>();
      bool has_mask = ad_out.has("mask") && ad_out.get("mask").get<bool>();
      bool has_multibox_rois = has_roi && ad_out.has("multibox_rois")
                               && ad_out.get("multibox_rois").get<bool>();
      bool timeseries
          = ad_out.has("timeseries") && ad_out.get("timeseries").get<bool>();

      if (timeseries)
        ad_out.erase("timeseries");

      if (has_bbox)
        {
          ad_out.erase("nclasses");
          ad_out.erase("bbox");
        }

      if (output_params->best == nullptr)
        output_params->best = _best;

      if (!timeseries)
        best_cats(bcats, output_params->best, nclasses, has_bbox, has_roi,
                  has_mask);

      std::unordered_set<std::string> indexed_uris;
#ifdef USE_SIMSEARCH
      // index
      if (output_params->index)
        {
          // check whether index has been created
          if (!mlm->_se)
            {
              bool create_index = true;
              int index_dim = _best;
              if (has_roi)
                {
                  if (!bcats._vvcats.empty())
                    {
                      if (!bcats._vvcats.at(0)._vals.empty()
                          && (*bcats._vvcats.at(0)._vals.begin())
                                 .second.has("vals"))
                        index_dim = (*bcats._vvcats.at(0)._vals.begin())
                                        .second.get("vals")
                                        .get<std::vector<double>>()
                                        .size(); // lookup to the first roi
                                                 // dimensions
                      else
                        create_index = false;
                    }
                  else
                    create_index = false;
                }
              if (create_index)
                mlm->create_sim_search(index_dim, output_params);
            }

          // index output content
          if (!has_roi)
            {
#ifdef USE_FAISS
              std::vector<URIData> urids;
              std::vector<std::vector<double>> probsv;
#endif
              for (size_t i = 0; i < bcats._vvcats.size(); i++)
                {
                  std::vector<double> probs;
                  auto mit = bcats._vvcats.at(i)._cats.begin();
                  while (mit != bcats._vvcats.at(i)._cats.end())
                    {
                      probs.push_back((*mit).first);
                      ++mit;
                    }
                  URIData urid(bcats._vvcats.at(i)._label);
#ifdef USE_FAISS
                  urids.push_back(urid);
                  probsv.push_back(probs);
#else
                  mlm->_se->index(urid, probs);
#endif
                  indexed_uris.insert(urid._uri);
                }
#ifdef USE_FAISS
              mlm->_se->index(urids, probsv);
#endif
            }
          else // roi
            {
              int nrois = 0;
              for (size_t i = 0; i < bcats._vvcats.size(); i++)
                {
                  auto vit = bcats._vvcats.at(i)._vals.begin();
                  auto bit = bcats._vvcats.at(i)._bboxes.begin();
                  auto mit = bcats._vvcats.at(i)._cats.begin();
#ifdef USE_FAISS
                  std::vector<URIData> urids;
                  std::vector<std::vector<double>> datas;
#endif
                  while (mit != bcats._vvcats.at(i)._cats.end())
                    {
                      std::vector<double> bbox
                          = { (*bit).second.get("xmin").get<double>(),
                              (*bit).second.get("ymin").get<double>(),
                              (*bit).second.get("xmax").get<double>(),
                              (*bit).second.get("ymax").get<double>() };
                      double prob = (*mit).first;
                      std::string cat = (*mit).second;
                      URIData urid(bcats._vvcats.at(i)._label, bbox, prob,
                                   cat);
#ifdef USE_FAISS
                      urids.push_back(urid);
                      datas.push_back((*vit)
                                          .second.get("vals")
                                          .get<std::vector<double>>());
#else
                      mlm->_se->index(urid, (*vit)
                                                .second.get("vals")
                                                .get<std::vector<double>>());
#endif
                      ++mit;
                      ++vit;
                      ++bit;
                      ++nrois;
                      indexed_uris.insert(urid._uri);
                    }
#ifdef USE_FAISS
                  mlm->_se->index(urids, datas);
#endif
                }
            }
        }

      // build index
      if (output_params->build_index)
        {
          if (mlm->_se)
            mlm->build_index();
          else
            throw SimIndexException("Cannot build index if not created");
        }

      // search
      if (output_params->search)
        {
          // check whether index has been created
          if (!mlm->_se)
            {
              int index_dim = _best;
              if (has_roi && !bcats._vvcats.at(0)._vals.empty())
                {
                  index_dim
                      = (*bcats._vvcats.at(0)._vals.begin())
                            .second.get("vals")
                            .get<std::vector<double>>()
                            .size(); // lookup to the first roi dimensions
                  mlm->create_sim_search(index_dim, output_params);
                }
            }

          int search_nn = _best;
          if (has_roi)
            search_nn = _search_nn;
          if (output_params->search_nn)
            search_nn = output_params->search_nn;
#ifdef USE_FAISS
          if (output_params->nprobe)
            mlm->_se->_tse->_nprobe = output_params->nprobe;
#endif
          if (!has_roi)
            {
              for (size_t i = 0; i < bcats._vvcats.size(); i++)
                {
                  std::vector<double> probs;
                  auto mit = bcats._vvcats.at(i)._cats.begin();
                  while (mit != bcats._vvcats.at(i)._cats.end())
                    {
                      probs.push_back((*mit).first);
                      ++mit;
                    }
                  std::vector<URIData> nn_uris;
                  std::vector<double> nn_distances;
                  mlm->_se->search(probs, search_nn, nn_uris, nn_distances);
                  for (size_t j = 0; j < nn_uris.size(); j++)
                    {
                      bcats._vvcats.at(i).add_nn(nn_distances.at(j),
                                                 nn_uris.at(j));
                    }
                }
            }
          else if (has_roi && has_multibox_rois)
            {
              for (size_t i = 0; i < bcats._vvcats.size(); i++)
                {
                  std::unordered_map<std::string, std::pair<double, int>>
                      multibox_nn; // one uri (image) / total distance, count
                  std::unordered_map<std::string,
                                     std::pair<double, int>>::iterator hit;
                  auto vit = bcats._vvcats.at(i)._vals.begin();
                  auto mit = bcats._vvcats.at(i)._cats.begin();
                  while (mit
                         != bcats._vvcats.at(i)
                                ._cats
                                .end()) // equivalent to iterating the bboxes
                    {
                      std::vector<URIData> nn_uris;
                      std::vector<double> nn_distances;
                      mlm->_se->search(
                          (*vit).second.get("vals").get<std::vector<double>>(),
                          search_nn, nn_uris, nn_distances);
                      for (size_t j = 0; j < nn_uris.size(); j++)
                        {
                          if ((hit = multibox_nn.find(nn_uris.at(j)._uri))
                              == multibox_nn.end())
                            {
                              double mb_dist = multibox_distance(
                                  nn_distances.at(j), nn_uris.at(j)._prob);
                              multibox_nn.insert(
                                  std::pair<std::string,
                                            std::pair<double, int>>(
                                      nn_uris.at(j)._uri,
                                      std::pair<double, int>(mb_dist, 1)));
                            }
                          else
                            {
                              (*hit).second.first += multibox_distance(
                                  nn_distances.at(j), nn_uris.at(j)._prob);
                              (*hit).second.second += 1;
                            }
                        }

                      ++mit;
                      ++vit;
                    }
                  // final ranking per images and store final results here
                  hit = multibox_nn.begin();
                  while (hit != multibox_nn.end()) // unsorted
                    {
                      // std::cerr << "adding uri=" << (*hit).first << " /
                      // dist=" << (*hit).second.first /
                      // static_cast<double>((*hit).second.second) <<
                      // std::endl;
                      bcats._vvcats.at(i).add_nn(
                          (*hit).second.first
                              / static_cast<double>((*hit).second.second),
                          URIData((*hit).first)); // sorted as per multimap
                      ++hit;
                    }
                }
            }
          else // has_roi
            {
              for (size_t i = 0; i < bcats._vvcats.size(); i++)
                {
                  int bb = 0;
                  auto vit = bcats._vvcats.at(i)._vals.begin();
                  auto mit = bcats._vvcats.at(i)._cats.begin();
                  while (mit
                         != bcats._vvcats.at(i)
                                ._cats
                                .end()) // equivalent to iterating the bboxes
                    {
                      std::vector<URIData> nn_uris;
                      std::vector<double> nn_distances;
                      mlm->_se->search(
                          (*vit).second.get("vals").get<std::vector<double>>(),
                          search_nn, nn_uris, nn_distances);
                      ++mit;
                      ++vit;
                      for (size_t j = 0; j < nn_uris.size(); j++)
                        {
                          bcats._vvcats.at(i).add_bbox_nn(
                              bb, nn_distances.at(j), nn_uris.at(j));
                        }
                      ++bb;
                    }
                }
            }
        }
#endif

      if (has_multibox_rois)
        has_roi = false;
      if (!timeseries)
        bcats.to_ad(ad_out, regression, autoencoder, has_bbox, has_roi,
                    has_mask, timeseries, indexed_uris);
      else
        to_ad(ad_out, regression, autoencoder, has_bbox, has_roi, has_mask,
              timeseries, indexed_uris);
    }

    struct PredictionAndAnswer
    {
      float prediction;
      unsigned char answer; // this is either 0 or 1
    };

    // measure
    static void measure(const APIData &ad_res, const APIData &ad_out,
                        APIData &out, size_t test_id = 0,
                        const std::string test_name = "")
    {
      APIData meas_out;
      bool tloss = ad_res.has("train_loss");
      bool lr = ad_res.has("learning_rate");
      bool loss = ad_res.has("loss");
      bool iter = ad_res.has("iteration");
      bool regression = ad_res.has("regression");
      bool segmentation = ad_res.has("segmentation");
      bool multilabel = ad_res.has("multilabel");
      bool net_meas = ad_res.has("net_meas");
      bool bbox = ad_res.has("bbox");
      bool timeserie = ad_res.has("timeserie");
      bool autoencoder = ad_res.has("autoencoder");
      int timeseries = -1;
      if (timeserie)
        timeseries = ad_res.get("timeseries").get<int>();

      if (ad_out.has("measure"))
        {
          std::vector<std::string> measures
              = ad_out.get("measure").get<std::vector<std::string>>();
          bool bauc = (std::find(measures.begin(), measures.end(), "auc")
                       != measures.end());
          bool bacc = false;

          if (!multilabel && !segmentation && !net_meas && !bbox)
            {
              for (auto s : measures)
                if (s.find("acc") != std::string::npos)
                  {
                    bacc = true;
                    break;
                  }
            }
          bool bf1 = (std::find(measures.begin(), measures.end(), "f1")
                      != measures.end());
          bool bf1full = (std::find(measures.begin(), measures.end(), "f1full")
                          != measures.end());
          bool bmcll = (std::find(measures.begin(), measures.end(), "mcll")
                        != measures.end());
          bool bgini = (std::find(measures.begin(), measures.end(), "gini")
                        != measures.end());
          bool beucll = false;
          float beucll_thres = -1;
          find_presence_and_thres("eucll", measures, beucll, beucll_thres);
          bool bl1 = (std::find(measures.begin(), measures.end(), "l1")
                      != measures.end());
          bool bpercent
              = (std::find(measures.begin(), measures.end(), "percent")
                 != measures.end());
          bool compute_all_distl = (beucll || bl1 || bpercent) && !autoencoder;

          bool bmcc = (std::find(measures.begin(), measures.end(), "mcc")
                       != measures.end());
          bool baccv = false;
          bool mlacc = false;
          bool mlsoft_kl = false;
          float mlsoft_kl_thres = -1;
          bool mlsoft_js = false;
          float mlsoft_js_thres = -1;
          bool mlsoft_was = false;
          float mlsoft_was_thres = -1;
          bool mlsoft_ks = false;
          float mlsoft_ks_thres = -1;
          bool mlsoft_dc = false;
          float mlsoft_dc_thres = -1;
          bool mlsoft_r2 = false;
          float mlsoft_r2_thres = -1;
          bool mlsoft_deltas = false;
          float mlsoft_deltas_thres = -1;
          bool raw = false;

          raw = std::find(measures.begin(), measures.end(), "raw")
                != measures.end();

          if (segmentation)
            baccv = (std::find(measures.begin(), measures.end(), "acc")
                     != measures.end());
          if (multilabel && !regression)
            mlacc = (std::find(measures.begin(), measures.end(), "acc")
                     != measures.end());
          if (multilabel && regression)
            {
              bool acc = (std::find(measures.begin(), measures.end(), "acc")
                          != measures.end());
              if (acc)
                {
                  mlsoft_kl = mlsoft_js = mlsoft_was = mlsoft_ks = mlsoft_dc
                      = mlsoft_r2 = mlsoft_deltas = true;
                }
              else
                {
                  find_presence_and_thres("kl", measures, mlsoft_kl,
                                          mlsoft_kl_thres);
                  find_presence_and_thres("js", measures, mlsoft_js,
                                          mlsoft_js_thres);
                  find_presence_and_thres("was", measures, mlsoft_was,
                                          mlsoft_was_thres);
                  find_presence_and_thres("ks", measures, mlsoft_ks,
                                          mlsoft_ks_thres);
                  find_presence_and_thres("dc", measures, mlsoft_dc,
                                          mlsoft_dc_thres);
                  find_presence_and_thres("r2", measures, mlsoft_r2,
                                          mlsoft_r2_thres);
                  find_presence_and_thres("deltas", measures, mlsoft_deltas,
                                          mlsoft_deltas_thres);
                }
            }
          if (bbox)
            {
              bool bbmap = (std::find(measures.begin(), measures.end(), "map")
                            != measures.end());
              if (bbmap)
                {
                  std::map<int, float> aps;
                  double bmap = ap(ad_res, aps);
                  meas_out.add("map", bmap);
                  for (auto ap : aps)
                    {
                      std::string s = "map_" + std::to_string(ap.first);
                      meas_out.add(s, static_cast<double>(ap.second));
                    }
                }
              bool raw = (std::find(measures.begin(), measures.end(), "raw")
                          != measures.end());
              if (raw)
                {
                  std::vector<std::string> clnames
                      = ad_res.get("clnames").get<std::vector<std::string>>();
                  APIData ap = raw_detection_results(ad_res, clnames);
                  meas_out.add("raw", ap);
                }
            }
          if (net_meas) // measure is coming from the net directly
            {
              double acc = straight_meas(ad_res);
              meas_out.add("acc", acc);
            }
          if (bauc) // XXX: applies two binary classification problems only
            {
              double mauc = auc(ad_res);
              meas_out.add("auc", mauc);
            }
          if (bacc)
            {
              std::map<std::string, double> accs = acc(ad_res, measures);
              auto mit = accs.begin();
              while (mit != accs.end())
                {
                  meas_out.add((*mit).first, (*mit).second);
                  ++mit;
                }
            }
          if (baccv)
            {
              double meanacc, meaniou;
              std::vector<double> clacc;
              std::vector<double> cliou;
              double accs = acc_v(ad_res, meanacc, meaniou, clacc, cliou);
              meas_out.add("acc", accs);
              meas_out.add("meanacc", meanacc);
              meas_out.add("meaniou", meaniou);
              meas_out.add("clacc", clacc);
              meas_out.add("cliou", cliou);
            }
          if (mlacc)
            {
              double f1, sensitivity, specificity, harmmean, precision;
              multilabel_acc(ad_res, sensitivity, specificity, harmmean,
                             precision, f1);
              meas_out.add("f1", f1);
              meas_out.add("precision", precision);
              meas_out.add("sensitivity", sensitivity);
              meas_out.add("specificity", specificity);
              meas_out.add("harmmean", harmmean);
            }
          if (mlsoft_kl)
            {
              double kl_divergence = multilabel_soft_kl(
                  ad_res, -1); // kl: amount of lost info if using pred instead
                               // of truth
              meas_out.add("kl_divergence", kl_divergence);
              double kl_divergence_thres
                  = multilabel_soft_kl(ad_res, mlsoft_kl_thres);
              std::string b
                  = "kl_divergence_no_" + std::to_string(mlsoft_kl_thres);
              meas_out.add(b, kl_divergence_thres);
            }
          if (mlsoft_js)
            {
              double js_divergence = multilabel_soft_js(
                  ad_res, -1); // jsd: symetrized version of kl
              meas_out.add("js_divergence", js_divergence);
              double js_divergence_thres
                  = multilabel_soft_js(ad_res, mlsoft_js_thres);
              std::string b
                  = "js_divergence_no_" + std::to_string(mlsoft_js_thres);
              meas_out.add(b, js_divergence_thres);
            }
          if (mlsoft_was)
            {
              double wasserstein
                  = multilabel_soft_was(ad_res, -1); // wasserstein distance
              meas_out.add("wasserstein", wasserstein);
              double wasserstein_thres
                  = multilabel_soft_was(ad_res, mlsoft_was_thres);
              std::string b
                  = "wasserstein_no_" + std::to_string(mlsoft_was_thres);
              meas_out.add(b, wasserstein_thres);
            }
          if (mlsoft_ks)
            {
              double kolmogorov_smirnov = multilabel_soft_ks(
                  ad_res,
                  -1); // kolmogorov-smirnov test aka max individual error
              meas_out.add("kolmogorov_smirnov", kolmogorov_smirnov);
              double kolmogorov_smirnov_thres
                  = multilabel_soft_ks(ad_res, mlsoft_ks_thres);
              std::string b = "kolmogorov_smirnov_no_"
                              + std::to_string(kolmogorov_smirnov_thres);
              meas_out.add(b, kolmogorov_smirnov_thres);
            }
          if (mlsoft_dc)
            {
              double distance_correlation = multilabel_soft_dc(
                  ad_res,
                  -1); // distance correlation , same as brownian correlation
              meas_out.add("distance_correlation", distance_correlation);
              double distance_correlation_thres
                  = multilabel_soft_dc(ad_res, mlsoft_dc_thres);
              std::string b = "distance_correlation_no_"
                              + std::to_string(mlsoft_dc_thres);
              meas_out.add(b, distance_correlation_thres);
            }
          if (mlsoft_r2)
            {
              double r_2 = multilabel_soft_r2(
                  ad_res, -1); // r_2 score: best  is 1, min is 0
              meas_out.add("r2", r_2);
              double r_2_thres = multilabel_soft_r2(ad_res, mlsoft_r2_thres);
              std::string b = "r2_no_" + std::to_string(mlsoft_r2_thres);
              meas_out.add(b, r_2_thres);
            }
          if (mlsoft_deltas)
            {
              std::vector<double> delta_scores{
                0, 0, 0, 0
              }; // delta-score , aka 1 if pred \in [truth-delta, truth+delta]
              std::vector<double> delta_scores_thres{
                0, 0, 0, 0
              }; // delta-score , aka 1 if pred \in [truth-delta, truth+delta]
              std::vector<double> deltas{ 0.05, 0.1, 0.2, 0.5 };
              multilabel_soft_deltas(ad_res, delta_scores, deltas, -1);
              multilabel_soft_deltas(ad_res, delta_scores_thres, deltas,
                                     mlsoft_deltas_thres);
              for (unsigned int i = 0; i < deltas.size(); ++i)
                {
                  std::ostringstream sstr;
                  sstr << "delta_score_" << deltas[i];
                  meas_out.add(sstr.str(), delta_scores[i]);
                  sstr.str("");
                  sstr << "delta_score_" << deltas[i] << "_no_"
                       << mlsoft_deltas_thres;
                  meas_out.add(sstr.str(), delta_scores_thres[i]);
                }
            }

          if (!multilabel && !segmentation && !bbox && (bf1 || bf1full))
            {
              double f1, precision, recall, acc;
              dMat conf_diag, conf_matrix;
              dVec precisionV, recallV, f1V;
              f1 = mf1(ad_res, precision, recall, acc, precisionV, recallV,
                       f1V, conf_diag, conf_matrix);
              meas_out.add("f1", f1);
              meas_out.add("precision", precision);
              meas_out.add("recall", recall);
              meas_out.add("accp", acc);
              if (std::find(measures.begin(), measures.end(), "f1full")
                  != measures.end())
                {
                  std::vector<double> allPrecisions;
                  std::vector<double> allRecalls;
                  std::vector<double> allF1s;
                  for (int i = 0; i < precisionV.size(); ++i)
                    {
                      allPrecisions.push_back(precisionV(i));
                      allRecalls.push_back(recallV(i));
                      allF1s.push_back(f1V(i));
                    }
                  meas_out.add("precisions", allPrecisions);
                  meas_out.add("recalls", allRecalls);
                  meas_out.add("f1s", allF1s);
                  if (std::find(measures.begin(), measures.end(), "cmdiag")
                      == measures.end())
                    meas_out.add(
                        "labels",
                        ad_res.get("clnames").get<std::vector<std::string>>());
                }

              if (std::find(measures.begin(), measures.end(), "cmdiag")
                  != measures.end())

                {
                  std::vector<double> cmdiagv;
                  for (int i = 0; i < conf_diag.rows(); i++)
                    cmdiagv.push_back(conf_diag(i, 0));
                  meas_out.add("cmdiag", cmdiagv);
                  meas_out.add(
                      "labels",
                      ad_res.get("clnames").get<std::vector<std::string>>());
                }
              if (std::find(measures.begin(), measures.end(), "cmfull")
                  != measures.end())
                {
                  std::vector<std::string> clnames
                      = ad_res.get("clnames").get<std::vector<std::string>>();
                  std::vector<APIData> cmdata;
                  for (int i = 0; i < conf_matrix.cols(); i++)
                    {
                      std::vector<double> cmrow;
                      for (int j = 0; j < conf_matrix.rows(); j++)
                        cmrow.push_back(conf_matrix(j, i));
                      APIData adrow;
                      adrow.add(clnames.at(i), cmrow);
                      cmdata.push_back(adrow);
                    }
                  meas_out.add("cmfull", cmdata);
                }
            }
          if (!multilabel && !segmentation && !bbox && bmcll)
            {
              double mmcll = mcll(ad_res);
              meas_out.add("mcll", mmcll);
            }
          if (bgini)
            {
              double mgini = gini(ad_res, regression);
              meas_out.add("gini", mgini);
            }
          if (beucll)
            {
              double meucll;
              std::vector<double> all_meucll;
              std::tie(meucll, all_meucll)
                  = distl(ad_res, -1, compute_all_distl, false);
              meas_out.add("eucll", meucll);
              if (all_meucll.size() > 1 && compute_all_distl)
                for (unsigned int i = 0; i < all_meucll.size(); ++i)
                  meas_out.add("eucll_" + std::to_string(i), all_meucll[i]);

              if (beucll_thres > 0)
                {
                  std::tuple<double, std::vector<double>> tmeucll_thres
                      = distl(ad_res, beucll_thres, compute_all_distl, false);
                  double meucll_thres = std::get<0>(tmeucll_thres);
                  std::string b = "eucll_no_" + std::to_string(beucll_thres);
                  meas_out.add(b, meucll_thres);
                  std::vector<double> all_meucll_thres
                      = std::get<1>(tmeucll_thres);
                  if (all_meucll_thres.size() > 1)
                    for (unsigned int i = 0; i < all_meucll_thres.size(); ++i)
                      {
                        std::string b = "eucll_no_" + std::to_string(i) + "_"
                                        + std::to_string(beucll_thres);
                        meas_out.add(b, all_meucll_thres[i]);
                      }
                }
            }
          if (bl1)
            {
              double ml1;
              std::vector<double> all_ml1;
              std::tie(ml1, all_ml1)
                  = distl(ad_res, -1, compute_all_distl, true);
              meas_out.add("l1", ml1);
              for (unsigned int i = 0; i < all_ml1.size(); ++i)
                meas_out.add("l1_" + std::to_string(i), all_ml1[i]);
            }
          if (bpercent)
            {
              double mpercent;
              std::vector<double> all_mpercent;
              std::tie(mpercent, all_mpercent)
                  = percentl(ad_res, compute_all_distl);
              meas_out.add("percent", mpercent);
              for (unsigned int i = 0; i < all_mpercent.size(); ++i)
                meas_out.add("percent_" + std::to_string(i), all_mpercent[i]);
            }
          if (bmcc)
            {
              double mmcc = mcc(ad_res);
              meas_out.add("mcc", mmcc);
            }
          if (raw && !bbox)
            {
              APIData raw_res = raw_results(
                  ad_res,
                  ad_res.get("clnames").get<std::vector<std::string>>());
              meas_out.add("raw", raw_res);
            }
          if (timeserie)
            {
              // we have timeseries outputs (flattened / interleaved in preds!)
              std::vector<double> max_errors(timeseries);
              std::vector<int> indexes_max_error(timeseries);
              std::vector<double> mean_errors(timeseries);
              double max_error;
              double mean_error;

              bool L1 = false;
              bool L2 = false;
              bool smape = false;
              bool mape = false;
              bool mase = false;
              bool owa = false;
              bool mae = false;
              bool mse = false;
              bool L1_all = false;
              bool L2_all = false;
              bool smape_all = false;
              bool mape_all = false;
              bool mase_all = false;
              bool owa_all = false;
              bool mae_all = false;
              bool mse_all = false;
              if (ad_out.has("measure"))
                {
                  std::vector<std::string> measures
                      = ad_out.get("measure").get<std::vector<std::string>>();
                  L1 = (std::find(measures.begin(), measures.end(), "L1")
                        != measures.end());
                  L2 = (std::find(measures.begin(), measures.end(), "L2")
                        != measures.end());
                  smape = (std::find(measures.begin(), measures.end(), "smape")
                           != measures.end());
                  mape = (std::find(measures.begin(), measures.end(), "mape")
                          != measures.end());
                  mase = (std::find(measures.begin(), measures.end(), "mase")
                          != measures.end());
                  owa = (std::find(measures.begin(), measures.end(), "owa")
                         != measures.end());
                  mae = (std::find(measures.begin(), measures.end(), "mae")
                         != measures.end());
                  mse = (std::find(measures.begin(), measures.end(), "mse")
                         != measures.end());
                  L1_all
                      = (std::find(measures.begin(), measures.end(), "L1_all")
                         != measures.end());
                  L2_all
                      = (std::find(measures.begin(), measures.end(), "L2_all")
                         != measures.end());
                  smape_all = (std::find(measures.begin(), measures.end(),
                                         "smape_all")
                               != measures.end());
                  mape_all = (std::find(measures.begin(), measures.end(),
                                        "mape_all")
                              != measures.end());
                  mase_all = (std::find(measures.begin(), measures.end(),
                                        "mase_all")
                              != measures.end());
                  owa_all
                      = (std::find(measures.begin(), measures.end(), "owa_all")
                         != measures.end());
                  mae_all
                      = (std::find(measures.begin(), measures.end(), "mae_all")
                         != measures.end());
                  mse_all
                      = (std::find(measures.begin(), measures.end(), "mse_all")
                         != measures.end());
                }

              if (!L1 && !L2 && !smape && !mape && !mase && !owa && !mae
                  && !mse && !L1_all && !L2_all && !smape_all && !mape_all
                  && !mase_all && !owa_all && !mae_all && !mse_all)
                L1 = true;

              if (L1 || L1_all)
                {
                  timeSeriesErrors(ad_res, timeseries, &max_errors[0],
                                   &indexes_max_error[0], &mean_errors[0],
                                   max_error, mean_error, true);

                  if (L1_all)
                    {
                      for (int i = 0; i < timeseries; ++i)
                        {
                          meas_out.add("L1_max_error_" + std::to_string(i),
                                       max_errors[i]);
                          meas_out.add("L1_max_error_" + std::to_string(i)
                                           + "_date",
                                       (double)(indexes_max_error[i]));
                          meas_out.add("L1_mean_error_" + std::to_string(i),
                                       mean_errors[i]);
                        }
                    }
                  meas_out.add("L1_max_error", max_error);
                  meas_out.add("L1_mean_error", mean_error);
                  if (!L2 && !L2_all)
                    meas_out.add("eucll", mean_error);
                }

              if (L2 || L2_all)
                {
                  timeSeriesErrors(ad_res, timeseries, &max_errors[0],
                                   &indexes_max_error[0], &mean_errors[0],
                                   max_error, mean_error, false);
                  if (L2_all)
                    {
                      for (int i = 0; i < timeseries; ++i)
                        {
                          meas_out.add("L2_max_error_" + std::to_string(i),
                                       max_errors[i]);
                          meas_out.add("L2_max_error_" + std::to_string(i)
                                           + "_date",
                                       (double)(indexes_max_error[i]));
                          meas_out.add("L2_mean_error_" + std::to_string(i),
                                       mean_errors[i]);
                        }
                    }
                  meas_out.add("L2_max_error", max_error);
                  meas_out.add("L2_mean_error", mean_error);
                  meas_out.add("eucll", mean_error);
                }
              if (mape || smape || mase || owa || mae || mse || mape_all
                  || smape_all || mase_all || owa_all || mae_all || mse_all)
                {
                  std::vector<double> mapev(timeseries);
                  std::vector<double> smapev(timeseries);
                  std::vector<double> masev(timeseries);
                  std::vector<double> owav(timeseries);
                  std::vector<double> maev(timeseries);
                  std::vector<double> msev(timeseries);
                  timeSeriesMetrics(ad_res, timeseries, mapev, smapev, masev,
                                    owav, maev, msev);
                  double maped = 0.0;
                  double smaped = 0.0;
                  double mased = 0.0;
                  double owad = 0.0;
                  double maed = 0.0;
                  double msed = 0.0;
                  for (int i = 0; i < timeseries; ++i)
                    {
                      maped += mapev[i];
                      smaped += smapev[i];
                      mased += masev[i];
                      owad += owav[i];
                      maed += maev[i];
                      msed += msev[i];
                      if (mape_all)
                        meas_out.add("MAPE_" + std::to_string(i), mapev[i]);
                      if (smape_all)
                        meas_out.add("sMAPE_" + std::to_string(i), smapev[i]);
                      if (mase_all)
                        meas_out.add("MASE_" + std::to_string(i), masev[i]);
                      if (owa_all)
                        meas_out.add("OWA_" + std::to_string(i), owav[i]);
                      if (mae_all)
                        meas_out.add("MAE_" + std::to_string(i), maev[i]);
                      if (mse_all)
                        meas_out.add("MSE_" + std::to_string(i), msev[i]);
                    }
                  maped /= timeseries;
                  smaped /= timeseries;
                  mased /= timeseries;
                  owad /= timeseries;
                  maed /= timeseries;
                  msed /= timeseries;
                  if (mape)
                    meas_out.add("MAPE", maped);
                  if (smape)
                    meas_out.add("sMAPE", smaped);
                  if (mase)
                    meas_out.add("MASE", mased);
                  if (owa)
                    meas_out.add("OWA", owad);
                  if (mae)
                    meas_out.add("MAE", maed);
                  if (mse)
                    meas_out.add("MSE", msed);
                }
            }
        }
      if (loss)
        meas_out.add("loss",
                     ad_res.get("loss")
                         .get<double>()); // 'universal', comes from algorithm
      if (tloss)
        meas_out.add("train_loss", ad_res.get("train_loss").get<double>());
      if (iter)
        meas_out.add("iteration", ad_res.get("iteration").get<double>());
      if (lr)
        meas_out.add("learning_rate",
                     ad_res.get("learning_rate").get<double>());

      meas_out.add("test_id", static_cast<int>(test_id));
      meas_out.add("test_name", test_name);

      std::vector<APIData> measures = out.has("measures")
                                          ? out.getv("measures")
                                          : std::vector<APIData>();
      measures.push_back(meas_out);
      out.add("measures", measures);

      if (test_id == 0)
        out.add("measure", meas_out);
    }

    /** Reduce metrics over multiple test sets. The aggregated metrics are used
     * to determine the best model. */
    static void aggregate_multiple_testsets(APIData &ad_out)
    {
      APIData meas_obj;
      std::vector<APIData> measures = ad_out.getv("measures");
      std::unordered_map<std::string, double> measures_sum;

      for (size_t i = 0; i < measures.size(); ++i)
        {
          auto &test_meas = measures.at(i);
          for (std::string key : test_meas.list_keys())
            {
              if (test_meas.get(key).is<double>())
                {
                  double val = test_meas.get(key).get<double>();
                  auto it = measures_sum.insert({ key, 0.0 });
                  it.first->second += val;
                }
            }
        }

      for (auto &e : measures_sum)
        {
          e.second /= measures.size();
          meas_obj.add(e.first, e.second);
        }
      ad_out.add("measure", meas_obj);
    }

    static void
    timeSeriesMetrics(const APIData &ad, const int timeseries,
                      std::vector<double> &mape, std::vector<double> &smape,
                      std::vector<double> &mase, std::vector<double> &owa,
                      std::vector<double> &mae, std::vector<double> &mse)
    {
      Eigen::Map<dVec> global_mape_vector = dVec::Map(mape.data(), timeseries);
      global_mape_vector.setZero();
      Eigen::Map<dVec> global_smape_vector
          = dVec::Map(smape.data(), timeseries);
      global_smape_vector.setZero();
      Eigen::Map<dVec> global_mase_vector = dVec::Map(mase.data(), timeseries);
      global_mase_vector.setZero();
      Eigen::Map<dVec> owa_vector = dVec::Map(owa.data(), timeseries);
      owa_vector.setZero();
      Eigen::Map<dVec> mae_vector = dVec::Map(mae.data(), timeseries);
      mae_vector.setZero();
      Eigen::Map<dVec> mse_vector = dVec::Map(mse.data(), timeseries);
      mse_vector.setZero();

      dVec global_smape_naive_vector(timeseries);
      global_smape_naive_vector.setZero();

      double nts = 0;
      int batch_size = ad.get("batch_size").get<int>();
      for (int i = 0; i < batch_size; i++)
        {
          APIData bad = ad.getobj(std::to_string(i));
          std::vector<double> targets
              = bad.get("target").get<std::vector<double>>();
          std::vector<double> targets_unscaled
              = bad.get("target_unscaled").get<std::vector<double>>();

          std::vector<double> predictions
              = bad.get("pred").get<std::vector<double>>();
          std::vector<double> predictions_unscaled
              = bad.get("pred_unscaled").get<std::vector<double>>();

          nts += targets.size();

          int dataduration = targets.size() / timeseries;
          typedef Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic,
                                Eigen::RowMajor>
              dMat;
          dMat dpred = dMat::Map(&predictions.at(0), dataduration, timeseries);
          dMat dtarg = dMat::Map(&targets.at(0), dataduration, timeseries);
          dMat dpred_unscaled = dMat::Map(&predictions_unscaled.at(0),
                                          dataduration, timeseries);
          dMat dtarg_unscaled
              = dMat::Map(&targets_unscaled.at(0), dataduration, timeseries);
          // now can access dpred(timestep_number, serie_number)
          dMat error;
          error = (dpred - dtarg).cwiseAbs();
          dMat error_unscaled;
          error_unscaled = (dpred_unscaled - dtarg_unscaled).cwiseAbs();
          dMat square_error_unscaled;
          square_error_unscaled = (dpred_unscaled - dtarg_unscaled).cwiseAbs();
          square_error_unscaled
              = square_error_unscaled.array().square().matrix();

          // error of first term is random in case of LSTM, which can be
          // huge after normalization
          error.row(0).setZero();

          dMat dprednaive(dataduration, timeseries);
          dprednaive.block(0, 0, 1, timeseries)
              = dtarg.block(0, 0, 1, timeseries);
          dprednaive.block(1, 0, dataduration - 1, timeseries)
              = dtarg.block(0, 0, dataduration - 1, timeseries);

          dMat errornaive = (dprednaive - dtarg).cwiseAbs();

          dMat predAbs = dpred.cwiseAbs();
          dMat targAbs = dtarg.cwiseAbs();
          dMat dprednaiveAbs = dprednaive.cwiseAbs();

          dVec mape_vector
              = (error.cwiseQuotient(
                     (targAbs.array() + TS_METRICS_EPSILON).matrix()))
                    .colwise()
                    .sum();

          mape_vector /= static_cast<float>(dataduration);
          global_mape_vector += mape_vector;

          mae_vector += error_unscaled.colwise().sum()
                        / static_cast<float>(targets.size());
          mse_vector += square_error_unscaled.colwise().sum()
                        / static_cast<float>(targets.size());

          dVec smape_vector = (error.cwiseQuotient(((predAbs + targAbs).array()
                                                    + TS_METRICS_EPSILON)
                                                       .matrix()))
                                  .colwise()
                                  .sum();
          smape_vector /= static_cast<float>(dataduration);
          global_smape_vector += smape_vector;

          dVec sumerrornaive
              = errornaive.colwise().sum() / static_cast<float>(dataduration);

          dVec ecs = error.colwise().sum() / static_cast<float>(dataduration);

          dVec mase_vector = ecs.cwiseQuotient(
              (sumerrornaive.array() + TS_METRICS_EPSILON).matrix());
          mase_vector /= static_cast<float>(dataduration);
          global_mase_vector += mase_vector;

          dVec smape_naive_vector
              = (errornaive.cwiseQuotient(
                     ((targAbs + dprednaiveAbs).array() + TS_METRICS_EPSILON)
                         .matrix()))
                    .colwise()
                    .sum();
          smape_naive_vector /= static_cast<float>(dataduration);
          global_smape_naive_vector += smape_naive_vector;
        }

      global_mape_vector /= static_cast<float>(batch_size);
      global_mape_vector *= 100;
      global_smape_vector /= static_cast<float>(batch_size);
      global_smape_vector *= 200.0;
      global_mase_vector /= static_cast<float>(batch_size);
      global_smape_naive_vector /= static_cast<float>(batch_size);
      global_smape_naive_vector *= 200.0;
      owa_vector
          = (global_smape_vector.cwiseQuotient(
                 (global_smape_naive_vector.array() + TS_METRICS_EPSILON)
                     .matrix())
             + global_mase_vector);
      owa_vector /= 2.0;
      mae_vector /= static_cast<float>(batch_size);
      mse_vector /= static_cast<float>(batch_size);
    }

    static void timeSeriesErrors(const APIData &ad, const int timeseries,
                                 double *const max_errors,
                                 int *const indexes_max_error,
                                 double *const mean_errors, double &max_error,
                                 double &mean_error, bool L1)
    {
      Eigen::Map<dVec> mean_error_vector = dVec::Map(mean_errors, timeseries);
      Eigen::Map<Eigen::VectorXi> idxmxerrors
          = Eigen::VectorXi::Map(indexes_max_error, timeseries);
      mean_error_vector.setZero();
      Eigen::Map<dVec> max = dVec::Map(max_errors, timeseries);
      double nts = 0;
      int batch_size = ad.get("batch_size").get<int>();
      for (int i = 0; i < batch_size; i++)
        {
          APIData bad = ad.getobj(std::to_string(i));
          std::vector<double> targets
              = bad.get("target").get<std::vector<double>>();
          /* std::cout << "targets: " ; */
          /* for (double d: targets) */
          /*   std::cout << d << " "; */
          /* std::cout << std::endl; */
          std::vector<double> predictions
              = bad.get("pred").get<std::vector<double>>();
          /* std::cout << "predictions: "; */
          /* for (double d : predictions) */
          /*   std::cout << d << " "; */
          /* std::cout << std::endl; */
          nts += targets.size();

          int dataduration = targets.size() / timeseries;
          typedef Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic,
                                Eigen::RowMajor>
              dMat;
          dMat dpred = dMat::Map(&predictions.at(0), dataduration, timeseries);
          dMat dtarg = dMat::Map(&targets.at(0), dataduration, timeseries);
          // now can access dpred(timestep_number, serie_number)
          dMat error;
          error = (dpred - dtarg).cwiseAbs();
          if (!L1)
            error = error.array() * error.array();
          //          std::cout << "error: " << error << std::endl;
          dVec batchmax = error.colwise().maxCoeff();
          std::vector<int> batch_max_indexes(timeseries);
          for (int j = 0; j < timeseries; ++j)
            error.col(j).maxCoeff(&(batch_max_indexes[j]));

          mean_error_vector += error.colwise().sum();
          if (!L1)
            mean_error_vector = mean_error_vector.array().sqrt();
          if (i == 0)
            {
              max = batchmax;
              for (int j = 0; j < timeseries; ++j)
                idxmxerrors(j) = batch_max_indexes[j];
            }
          else
            {
              for (int j = 0; j < timeseries; ++j)
                {
                  if (batchmax(j) > max(j))
                    {
                      max(j) = batchmax(j);
                      idxmxerrors(j) = batch_max_indexes[j];
                    }
                }
            }
        }
      mean_error_vector /= static_cast<float>(nts);
      mean_error_vector *= timeseries;

      max_error = max_errors[0];
      mean_error = mean_errors[0];
      for (int i = 1; i < timeseries; ++i)
        {
          if (max_errors[i] > max_error)
            max_error = max_errors[i];
          mean_error += mean_errors[i];
        }
      mean_error /= timeseries;
    }

    static void find_presence_and_thres(std::string meas,
                                        std::vector<std::string> measures,
                                        bool &do_meas, float &meas_thres)
    {
      for (auto s : measures)
        if (s.find(meas) != std::string::npos)
          {
            do_meas = true;
            std::vector<std::string> sv = dd_utils::split(s, '-');
            if (sv.size() == 2)
              {
                meas_thres = std::atof(sv.at(1).c_str());
              }
            else
              meas_thres = 0.0;
          }
    }

    static double straight_meas(const APIData &ad)
    {
      APIData bad = ad.getobj("0");
      std::vector<double> acc = bad.get("pred").get<std::vector<double>>();
      if (acc.empty())
        return 0.0;
      else
        return acc.at(0);
    }

    // measure: ACC
    static std::map<std::string, double>
    acc(const APIData &ad, const std::vector<std::string> &measures)
    {
      struct acc_comp
      {
        acc_comp(const std::vector<double> &v) : _v(v)
        {
        }
        bool operator()(double a, double b)
        {
          return _v[a] > _v[b];
        }
        const std::vector<double> _v;
      };
      std::map<std::string, double> accs;
      std::vector<int> vacck;
      for (auto s : measures)
        if (s.find("acc") != std::string::npos)
          {
            std::vector<std::string> sv = dd_utils::split(s, '-');
            if (sv.size() == 2)
              {
                vacck.push_back(std::atoi(sv.at(1).c_str()));
              }
            else
              vacck.push_back(1);
          }

      int batch_size = ad.get("batch_size").get<int>();
      for (auto k : vacck)
        {
          double acc = 0.0;
          for (int i = 0; i < batch_size; i++)
            {
              APIData bad = ad.getobj(std::to_string(i));
              std::vector<double> predictions
                  = bad.get("pred").get<std::vector<double>>();
              if (k - 1 >= static_cast<int>(predictions.size()))
                continue; // ignore instead of error
              std::vector<int> predk(predictions.size());
              for (size_t j = 0; j < predictions.size(); j++)
                predk[j] = j;
              std::partial_sort(predk.begin(), predk.begin() + k - 1,
                                predk.end(), acc_comp(predictions));
              for (int l = 0; l < k; l++)
                if (predk.at(l) == bad.get("target").get<double>())
                  {
                    acc++;
                    break;
                  }
            }
          std::string key = "acc";
          if (k > 1)
            key += "-" + std::to_string(k);
          accs.insert(std::pair<std::string, double>(
              key, acc / static_cast<double>(batch_size)));
        }
      return accs;
    }

    static double acc_v(const APIData &ad, double &meanacc, double &meaniou,
                        std::vector<double> &clacc, std::vector<double> &cliou)
    {
      int nclasses = ad.get("nclasses").get<int>();
      int batch_size = ad.get("batch_size").get<int>();
      std::vector<double> mean_acc(nclasses, 0.0);
      std::vector<double> mean_acc_bs(nclasses, 0.0);
      std::vector<double> mean_iou_bs(nclasses, 0.0);
      std::vector<double> mean_iou(nclasses, 0.0);
      double acc_v = 0.0;
      meanacc = 0.0;
      meaniou = 0.0;
      for (int i = 0; i < batch_size; i++)
        {
          APIData bad = ad.getobj(std::to_string(i));
          std::vector<double> predictions
              = bad.get("pred").get<std::vector<double>>(); // all best-1
          std::vector<double> targets
              = bad.get("target").get<std::vector<double>>(); // all targets
                                                              // against best-1
          dVec dpred = dVec::Map(&predictions.at(0), predictions.size());
          dVec dtarg = dVec::Map(&targets.at(0), targets.size());
          dVec ddiff = dpred - dtarg;
          double acc = (ddiff.cwiseAbs().array() == 0).count()
                       / static_cast<double>(dpred.size());
          acc_v += acc;

          for (int c = 0; c < nclasses; c++)
            {
              dVec dpredc
                  = (dpred.array() == c)
                        .select(dpred, dVec::Constant(dpred.size(), -2.0));
              dVec dtargc
                  = (dtarg.array() == c)
                        .select(dtarg, dVec::Constant(dtarg.size(), -1.0));
              dVec ddiffc = dpredc - dtargc;
              double c_sum = (ddiffc.cwiseAbs().array() == 0).count();

              // mean acc over classes
              double c_total_targ
                  = static_cast<double>((dtarg.array() == c).count());
              if (/* c_sum == 0 || */ c_total_targ == 0)
                {
                }
              else
                {
                  double accc = c_sum / c_total_targ;
                  mean_acc[c] += accc;
                  mean_acc_bs[c]++;
                }

              // mean intersection over union
              double c_false_neg
                  = static_cast<double>((ddiffc.array() == -2 - c).count());
              double c_false_pos
                  = static_cast<double>((ddiffc.array() == c + 1).count());
              // below corner case where nothing is to predict : put correct
              // to zero but do not devide by zero
              double iou = (c_sum == 0)
                               ? 0
                               : c_sum / (c_false_pos + c_sum + c_false_neg);
              mean_iou[c] += iou;
              // ... and divide one time less when normalizing by batch size
              if (c_total_targ != 0)
                mean_iou_bs[c]++;
              // another possible waywould be to put artificially iou to one
              // if nothing is to be predicted for class c
            }
        }
      int c_nclasses = 0;
      for (int c = 0; c < nclasses; c++)
        {
          if (mean_acc_bs[c] > 0.0)
            {
              mean_acc[c] /= mean_acc_bs[c];
              mean_iou[c] /= mean_iou_bs[c];
              c_nclasses++;
            }
          meanacc += mean_acc[c];
          meaniou += mean_iou[c];
        }
      clacc = mean_acc;
      cliou = mean_iou;

      // corner case where prediction is wrong
      if (c_nclasses > 0)
        {
          meanacc /= static_cast<double>(c_nclasses);
          meaniou /= static_cast<double>(c_nclasses);
        }
      return acc_v / static_cast<double>(batch_size);
    }

    // multilabel measures
    static double multilabel_acc(const APIData &ad, double &sensitivity,
                                 double &specificity, double &harmmean,
                                 double &precision, double &f1)
    {
      int batch_size = ad.get("batch_size").get<int>();
      double tp = 0.0;
      double fp = 0.0;
      double tn = 0.0;
      double fn = 0.0;
      double count_pos = 0.0;
      double count_neg = 0.0;
      for (int i = 0; i < batch_size; i++)
        {
          APIData bad = ad.getobj(std::to_string(i));
          std::vector<double> targets
              = bad.get("target").get<std::vector<double>>();
          std::vector<double> predictions
              = bad.get("pred").get<std::vector<double>>();
          for (size_t j = 0; j < predictions.size(); j++)
            {
              if (targets.at(j) < 0)
                continue;
              if (targets.at(j) >= 0.5)
                {
                  // positive accuracy
                  if (predictions.at(j) >= 0)
                    ++tp;
                  else
                    ++fn;
                  ++count_pos;
                }
              else
                {
                  // negative accuracy
                  if (predictions.at(j) < 0)
                    ++tn;
                  else
                    ++fp;
                  ++count_neg;
                }
            }
        }
      sensitivity = (count_pos > 0) ? tp / count_pos : 0.0;
      specificity = (count_neg > 0) ? tn / count_neg : 0.0;
      harmmean = ((count_pos + count_neg > 0))
                     ? 2 / (count_pos / tp + count_neg / tn)
                     : 0.0;
      precision = (tp > 0) ? (tp / (tp + fp)) : 0.0;
      f1 = (tp > 0) ? 2 * tp / (2 * tp + fp + fn) : 0.0;
      return f1;
    }

    static double multilabel_soft_kl(const APIData &ad, float thres)
    {
      double kl_divergence = 0;
      long int total_number = 0;
      int batch_size = ad.get("batch_size").get<int>();
      for (int i = 0; i < batch_size; i++)
        {
          APIData bad = ad.getobj(std::to_string(i));
          std::vector<double> targets
              = bad.get("target").get<std::vector<double>>();
          std::vector<double> predictions
              = bad.get("pred").get<std::vector<double>>();
          dVec dpred = dVec::Map(&predictions.at(0), predictions.size());
          dVec dtarg = dVec::Map(&targets.at(0), targets.size());
          double eps = 0.00001;
          dVec dprede = (dpred.array() < eps).select(eps, dpred);
          dVec dtarge = (dtarg.array() < eps).select(eps, dtarg);
          dVec temp = (dprede.array().inverse() * dtarge.array()).log()
                      * dtarge.array();
          if (thres >= 0)
            {
              total_number += (dtarg.array() > thres).count();
              temp = (dtarg.array() <= thres).select(0, temp);
            }
          else
            {
              total_number += (dtarg.array() >= 0).count();
              temp = (dtarg.array() < 0).select(0, temp);
            }
          kl_divergence += temp.sum();
        }
      return kl_divergence / (double)total_number;
    }

    static double multilabel_soft_js(const APIData &ad, float thres)
    {
      int batch_size = ad.get("batch_size").get<int>();
      double js_divergence = 0;
      long int total_number = 0;
      for (int i = 0; i < batch_size; i++)
        {
          APIData bad = ad.getobj(std::to_string(i));
          std::vector<double> targets
              = bad.get("target").get<std::vector<double>>();
          std::vector<double> predictions
              = bad.get("pred").get<std::vector<double>>();
          dVec dpred = dVec::Map(&predictions.at(0), predictions.size());
          dVec dtarg = dVec::Map(&targets.at(0), targets.size());
          double eps = 0.00001;
          dVec dprede = (dpred.array() < eps).select(eps, dpred);
          dVec dtarge = (dtarg.array() < eps).select(eps, dtarg);
          dVec temp = (dprede + dtarge).array().inverse();
          dVec temp2 = (temp.array() * dtarge.array() * 2).log()
                           * dtarge.array() * 0.5
                       + (temp.array() * dprede.array() * 2).log()
                             * dprede.array() * 0.5;
          if (thres >= 0)
            {
              total_number += (dtarg.array() > thres).count();
              temp2 = (dtarg.array() <= thres).select(0, temp2);
            }
          else
            {
              total_number += (dtarg.array() >= 0).count();
              temp2 = (dtarg.array() < 0).select(0, temp2);
            }
          js_divergence += temp2.sum();
        }
      return js_divergence / (double)total_number;
    }

    static double multilabel_soft_was(const APIData &ad, float thres)
    {
      int batch_size = ad.get("batch_size").get<int>();
      double was = 0;
      long int total_number = 0;
      for (int i = 0; i < batch_size; i++)
        {
          APIData bad = ad.getobj(std::to_string(i));
          std::vector<double> targets
              = bad.get("target").get<std::vector<double>>();
          std::vector<double> predictions
              = bad.get("pred").get<std::vector<double>>();
          dVec dpred = dVec::Map(&predictions.at(0), predictions.size());
          dVec dtarg = dVec::Map(&targets.at(0), targets.size());
          dVec dif = dtarg - dpred;
          if (thres >= 0)
            {
              dif = (dtarg.array() <= thres).select(0, dif);
              total_number += (dtarg.array() > thres).count();
            }
          else
            {
              dif = (dtarg.array() < 0).select(0, dif);
              total_number += (dtarg.array() >= 0).count();
            }
          was += (dif.array() * dif.array()).sum();
        }
      was = sqrt(was);
      return was / sqrt((double)total_number);
    }

    static double multilabel_soft_ks(const APIData &ad, float thres)
    {
      int batch_size = ad.get("batch_size").get<int>();
      double ks = 0;
      long int total_number = 0;
      for (int i = 0; i < batch_size; i++)
        {
          APIData bad = ad.getobj(std::to_string(i));
          std::vector<double> targets
              = bad.get("target").get<std::vector<double>>();
          std::vector<double> predictions
              = bad.get("pred").get<std::vector<double>>();
          dVec dpred = dVec::Map(&predictions.at(0), predictions.size());
          dVec dtarg = dVec::Map(&targets.at(0), targets.size());
          dVec dif = dtarg - dpred;
          if (thres >= 0)
            {
              dif = (dtarg.array() < thres).select(0, dif);
              total_number += (dtarg.array() > thres).count();
            }
          else
            {
              dif = (dtarg.array() < 0).select(0, dif);
              total_number += (dtarg.array() >= 0).count();
            }
          ks = dif.array().abs().maxCoeff();
        }
      return ks;
    }

    static int dc_pt_jk(const long int j, const long int k,
                        const std::vector<double> &targets,
                        const std::vector<double> &predictions, double &p_jk,
                        double &t_jk)
    {
      p_jk = fabs(predictions[j] - predictions[k]);
      t_jk = fabs(targets[j] - targets[k]);
      return 1;
    }

    static double multilabel_soft_dc(const APIData &ad, float thres)
    {
      int batch_size = ad.get("batch_size").get<int>();
      int nclasses = ad.getobj(std::to_string(0))
                         .get("target")
                         .get<std::vector<double>>()
                         .size();

      double distance_correlation = 0;

      std::vector<double> t_j(nclasses, 0);
      std::vector<double> p_j(nclasses, 0);
      double t_ = 0;
      double p_ = 0;

      double dcov = 0;
      double dvart = 0;
      double dvarp = 0;
      std::vector<int> care_classes;

      for (int i = 0; i < batch_size; ++i)
        {
          APIData badj = ad.getobj(std::to_string(i));
          std::vector<double> targets
              = badj.get("target").get<std::vector<double>>();
          std::vector<double> predictions
              = badj.get("pred").get<std::vector<double>>();

          care_classes.clear();
          for (int l = 0; l < nclasses; ++l)
            if (thres >= 0)
              {
                if (targets[l] > thres)
                  care_classes.push_back(l);
              }
            else if (targets[l] >= 0)
              care_classes.push_back(l);

          if (care_classes.size() == 0)
            continue;

          for (int l : care_classes)
            {
              for (int m : care_classes)
                {
                  double t_lm;
                  double p_lm;
                  dc_pt_jk(l, m, targets, predictions, p_lm, t_lm);
                  t_j[l] += t_lm;
                  p_j[l] += p_lm;
                }
              t_j[l] /= (double)care_classes.size();
              t_ += t_j[l];
              p_j[l] /= (double)care_classes.size();
              p_ += p_j[l];
            }
          t_ /= (double)care_classes.size();
          p_ /= (double)care_classes.size();

          for (int j : care_classes)
            for (int k : care_classes)
              {
                double p_jk;
                double t_jk;
                dc_pt_jk(j, k, targets, predictions, p_jk, t_jk);
                double p = p_jk - p_j[j] - p_j[k] + p_;
                double t = t_jk - t_j[j] - t_j[k] + t_;
                dcov += p * t;
                dvart += t * t;
                dvarp += p * p;
              }
          dcov /= care_classes.size() * care_classes.size();
          dvart /= care_classes.size() * care_classes.size();
          dvarp /= care_classes.size() * care_classes.size();
          dcov = sqrt(dcov);
          dvart = sqrt(dvart);

          dvarp = sqrt(dvarp);

          if (dvart != 0 && dvarp != 0)
            distance_correlation += dcov / (sqrt(dvart * dvarp));
        }
      distance_correlation /= batch_size;
      return distance_correlation;
    }

    static double multilabel_soft_r2(const APIData &ad, float thres)
    {
      int batch_size = ad.get("batch_size").get<int>();
      double tmean = 0;
      long int total_number = 0;
      double ssres = 0;
      for (int i = 0; i < batch_size; i++)
        {
          APIData bad = ad.getobj(std::to_string(i));
          std::vector<double> targets
              = bad.get("target").get<std::vector<double>>();
          std::vector<double> predictions
              = bad.get("pred").get<std::vector<double>>();
          dVec dpred = dVec::Map(&predictions.at(0), predictions.size());
          dVec dtarg = dVec::Map(&targets.at(0), targets.size());
          dVec dif = dtarg - dpred;
          if (thres >= 0)
            {
              total_number += (dtarg.array() > thres).count();
              tmean += ((dtarg.array() <= thres).select(0, dtarg)).sum();
              dif = (dtarg.array() <= thres).select(0, dif);
            }
          else
            {
              total_number += (dtarg.array() >= 0).count();
              tmean += ((dtarg.array() < 0).select(0, dtarg)).sum();
              dif = (dtarg.array() < 0).select(0, dif);
            }
          ssres += (dif.array() * dif.array()).sum();
        }
      tmean /= (double)total_number;

      double sstot = 0;

      for (int i = 0; i < batch_size; i++)
        {
          APIData bad = ad.getobj(std::to_string(i));
          std::vector<double> targets
              = bad.get("target").get<std::vector<double>>();
          std::vector<double> predictions
              = bad.get("pred").get<std::vector<double>>();
          dVec dpred = dVec::Map(&predictions.at(0), predictions.size());
          dVec dtarg = dVec::Map(&targets.at(0), targets.size());
          dVec temp;
          if (thres >= 0)
            {
              temp = (dtarg.array() <= thres).select(0, dtarg.array() - tmean);
            }
          else
            {
              temp = (dtarg.array() < 0).select(0, dtarg.array() - tmean);
            }
          sstot += (temp.array() * temp.array()).sum();
        }
      return 1.0 - ssres / sstot;
    }

    static void multilabel_soft_deltas(const APIData &ad,
                                       std::vector<double> &delta_scores,
                                       const std::vector<double> &deltas,
                                       float thres)
    {
      int batch_size = ad.get("batch_size").get<int>();
      long int total_number = 0;
      for (unsigned int k = 0; k < deltas.size(); ++k)
        delta_scores[k] = 0;
      for (int i = 0; i < batch_size; i++)
        {
          APIData bad = ad.getobj(std::to_string(i));
          std::vector<double> targets
              = bad.get("target").get<std::vector<double>>();
          std::vector<double> predictions
              = bad.get("pred").get<std::vector<double>>();
          dVec dpred = dVec::Map(&predictions.at(0), predictions.size());
          dVec dtarg = dVec::Map(&targets.at(0), targets.size());
          dVec dif;
          if (thres >= 0)
            {
              total_number += (dtarg.array() > thres).count();
              dif = (dtarg.array() <= thres)
                        .select(10, (dtarg - dpred).array().abs());
            }
          else
            {
              total_number += (dtarg.array() >= 0).count();
              dif = (dtarg.array() < 0)
                        .select(10, (dtarg - dpred).array().abs());
            }
          for (unsigned int k = 0; k < deltas.size(); ++k)
            delta_scores[k] += (dif.array() < deltas[k]).count();
        }
      for (unsigned int k = 0; k < deltas.size(); ++k)
        delta_scores[k] /= (double)total_number; // gives proportion of good
                                                 // in 0:1 at every threshold
    }

    static APIData raw_results(const APIData &ad,
                               std::vector<std::string> clnames)
    {
      APIData raw_res;
      int nclasses = ad.get("nclasses").get<int>();
      int batch_size = ad.get("batch_size").get<int>();
      std::vector<std::string> preds;
      std::vector<std::string> targets;
      std::vector<double> confs;
      std::vector<std::vector<double>> logits;
      for (int i = 0; i < batch_size; i++)
        {
          APIData bad = ad.getobj(std::to_string(i));
          std::vector<double> predictions
              = bad.get("pred").get<std::vector<double>>();
          double target = bad.get("target").get<double>();
          if (bad.has("logits"))
            logits.push_back(bad.get("logits").get<std::vector<double>>());
          if (target < 0)
            throw OutputConnectorBadParamException(
                "negative supervised discrete target (e.g. wrong use of "
                "label_offset ?");
          else if (target >= nclasses)
            throw OutputConnectorBadParamException(
                "target class has id " + std::to_string(target)
                + " is higher than the number of classes "
                + std::to_string(nclasses)
                + " (e.g. wrong number of classes specified with nclasses");
          // TODO : find max predictions -> argmax = estimation   max =
          // confidence
          targets.push_back(clnames[static_cast<int>(target)]);
          double max_pred = predictions[0];
          int best_cat = 0;
          for (unsigned int j = 1; j < predictions.size(); ++j)
            {
              if (predictions[j] > max_pred)
                {
                  best_cat = j;
                  max_pred = predictions[j];
                }
            }
          preds.push_back(clnames[static_cast<int>(best_cat)]);
          confs.push_back(max_pred);
        }
      raw_res.add("truths", targets);
      raw_res.add("estimations", preds);
      raw_res.add("confidences", confs);
      if (logits.size() > 0)
        {
          std::vector<APIData> adlogit;
          for (std::vector<double> l : logits)
            {
              APIData lad;
              lad.add("logits", l);
              adlogit.push_back(lad);
            }
          raw_res.add("all_logits", adlogit);
        }

      return raw_res;
    }

    // measure: F1
    static double mf1(const APIData &ad, double &precision, double &recall,
                      double &acc, dVec &precisionV, dVec &recallV, dVec &f1V,
                      dMat &conf_diag, dMat &conf_matrix)
    {
      int nclasses = ad.get("nclasses").get<int>();
      double f1 = 0.0;
      conf_matrix = dMat::Zero(nclasses, nclasses);
      int batch_size = ad.get("batch_size").get<int>();
      for (int i = 0; i < batch_size; i++)
        {
          APIData bad = ad.getobj(std::to_string(i));
          std::vector<double> predictions
              = bad.get("pred").get<std::vector<double>>();
          int maxpr = std::distance(
              predictions.begin(),
              std::max_element(predictions.begin(), predictions.end()));
          double target = bad.get("target").get<double>();
          if (target < 0)
            throw OutputConnectorBadParamException(
                "negative supervised discrete target (e.g. wrong use of "
                "label_offset ?");
          else if (target >= nclasses)
            throw OutputConnectorBadParamException(
                "target class has id " + std::to_string(target)
                + " is higher than the number of classes "
                + std::to_string(nclasses)
                + " (e.g. wrong number of classes specified with nclasses");
          conf_matrix(maxpr, static_cast<int>(target)) += 1.0;
        }
      conf_diag = conf_matrix.diagonal();
      dMat conf_csum = conf_matrix.colwise().sum();
      dMat conf_rsum = conf_matrix.rowwise().sum();
      dMat eps = dMat::Constant(nclasses, 1, 1e-8);
      acc = conf_diag.sum() / conf_matrix.sum();
      recallV
          = (conf_diag.transpose().cwiseQuotient(conf_csum + eps.transpose()))
                .transpose();
      recall = recallV.sum() / static_cast<double>(nclasses);
      precisionV = conf_diag.cwiseQuotient(conf_rsum + eps);
      precision = precisionV.sum() / static_cast<double>(nclasses);
      f1V = (2.0 * precisionV.cwiseProduct(recallV))
                .cwiseQuotient(precisionV + recallV + eps);
      f1 = f1V.sum() / static_cast<double>(nclasses);
      conf_diag = conf_diag.transpose()
                      .cwiseQuotient(conf_csum + eps.transpose())
                      .transpose();
      for (int i = 0; i < conf_matrix.cols(); i++)
        if (conf_csum(i) > 0)
          conf_matrix.col(i) /= conf_csum(i);
      return f1;
    }

    // measure: AP, mAP
    static void cumsum_pair(const std::vector<std::pair<double, int>> &pairs,
                            std::vector<int> &cumsum)
    {
      // Sort the pairs based on first item of the pair.
      std::vector<std::pair<double, int>> sort_pairs = pairs;
      std::stable_sort(sort_pairs.begin(), sort_pairs.end(),
                       SortScorePairDescend<int>);
      for (size_t i = 0; i < sort_pairs.size(); ++i)
        {
          if (i == 0)
            {
              cumsum.push_back(sort_pairs[i].second);
            }
          else
            {
              cumsum.push_back(cumsum.back() + sort_pairs[i].second);
            }
        }
    }

    static APIData raw_detection_results(const APIData &ad,
                                         std::vector<std::string> clnames)
    {
      APIData raw_res;
      std::vector<std::string> preds;
      std::vector<std::string> targets;
      std::vector<double> confs;
      std::vector<APIData> really_all_logits;
      bool output_logits = false;
      APIData bad = ad.getobj("0");
      int pos_count = ad.get("pos_count").get<int>();
      for (int i = 0; i < pos_count; i++)
        {
          std::vector<APIData> vbad = bad.getv(std::to_string(i));
          for (size_t j = 0; j < vbad.size(); j++)
            {
              std::vector<double> tp_d
                  = vbad.at(j).get("tp_d").get<std::vector<double>>();
              std::vector<int> tp_i
                  = vbad.at(j).get("tp_i").get<std::vector<int>>();
              std::vector<double> fp_d
                  = vbad.at(j).get("fp_d").get<std::vector<double>>();
              std::vector<int> fp_i
                  = vbad.at(j).get("fp_i").get<std::vector<int>>();
              int num_pos = vbad.at(j).get("num_pos").get<int>();
              int label = vbad.at(j).get("label").get<int>();

              // below true positives
              for (unsigned int k = 0; k < tp_d.size(); ++k)
                {
                  if (tp_i[k] == 1)
                    {
                      targets.push_back(clnames[label]);
                      preds.push_back(clnames[label]);
                      confs.push_back(tp_d[k]);
                    }
                  if (fp_i[k] == 1)
                    {
                      preds.push_back(clnames[label]);
                      targets.push_back(std::string("UNDEFINED_GT"));
                      confs.push_back(fp_d[k]);
                    }
                }
              // blow false negatives
              int ntp = 0;
              for (unsigned int k = 0; k < tp_i.size(); ++k)
                if (tp_i[k] == 1)
                  ntp++;
              for (int k = 0; k < (num_pos - ntp); ++k)
                {
                  targets.push_back(clnames[label]);
                  confs.push_back(1.0);
                  preds.push_back("NO_DETECTION");
                }

              if (vbad.at(j).has("all_logits"))
                {
                  output_logits = true;
                  const std::vector<APIData> &logits
                      = vbad.at(j).getv("all_logits");
                  really_all_logits.insert(really_all_logits.end(),
                                           logits.begin(), logits.end());
                  for (int k = 0; k < (num_pos - ntp); ++k)
                    {
                      APIData background_logits_ad;
                      std::vector<double> background_logits;
                      background_logits.push_back(
                          0.5 + 0.5 / (float)clnames.size());
                      for (unsigned int c = 1; c < clnames.size(); ++c)
                        background_logits.push_back(1.0 / (float)clnames.size()
                                                    / 2.0);
                      background_logits_ad.add("logits", background_logits);
                      really_all_logits.push_back(background_logits_ad);
                    }
                }
            }
        }
      raw_res.add("truths", targets);
      raw_res.add("estimations", preds);
      raw_res.add("confidences", confs);
      if (output_logits)
        raw_res.add("all_logits", really_all_logits);
      return raw_res;
    }

    static double compute_ap(const std::vector<std::pair<double, int>> &tp,
                             const std::vector<std::pair<double, int>> &fp,
                             const int &num_pos)
    {
      const double eps = 1e-6;
      const int num = tp.size();
      if (num == 0 || num_pos == 0)
        return 0.0;
      double ap = 0.0;
      std::vector<int> tp_cumsum;
      std::vector<int> fp_cumsum;
      cumsum_pair(tp, tp_cumsum); // tp cumsum
      cumsum_pair(fp, fp_cumsum); // fp cumsum
      std::vector<double> prec;   // precision
      for (int i = 0; i < num; i++)
        prec.push_back(static_cast<double>(tp_cumsum[i])
                       / (tp_cumsum[i] + fp_cumsum[i]));
      std::vector<double> rec; // recall
      for (int i = 0; i < num; i++)
        rec.push_back(static_cast<double>(tp_cumsum[i]) / num_pos);

      // voc12, ilsvrc style ap
      float cur_rec = rec.back();
      float cur_prec = prec.back();
      for (int i = num - 2; i >= 0; --i)
        {
          cur_prec = std::max<float>(prec[i], cur_prec);
          if (fabs(cur_rec - rec[i]) > eps)
            {
              ap += cur_prec * fabs(cur_rec - rec[i]);
            }
          cur_rec = rec[i];
        }
      ap += cur_rec * cur_prec;
      return ap;
    }

    static double ap(const APIData &ad, std::map<int, float> &APs)
    {
      double mmAP = 0.0;
      std::map<int, int> APs_count;
      int APs_count_all = 0;
      // extract tp, fp, labels
      APIData bad = ad.getobj("0");
      int pos_count = ad.get("pos_count").get<int>();
      for (int i = 0; i < pos_count; i++)
        {
          std::vector<APIData> vbad = bad.getv(std::to_string(i));
          for (size_t j = 0; j < vbad.size(); j++)
            {
              std::vector<double> tp_d
                  = vbad.at(j).get("tp_d").get<std::vector<double>>();
              std::vector<int> tp_i
                  = vbad.at(j).get("tp_i").get<std::vector<int>>();
              std::vector<double> fp_d
                  = vbad.at(j).get("fp_d").get<std::vector<double>>();
              std::vector<int> fp_i
                  = vbad.at(j).get("fp_i").get<std::vector<int>>();
              int num_pos = vbad.at(j).get("num_pos").get<int>();
              int label = vbad.at(j).get("label").get<int>();
              std::vector<std::pair<double, int>> tp;
              std::vector<std::pair<double, int>> fp;

              for (size_t j = 0; j < tp_d.size(); j++)
                {
                  tp.push_back(std::pair<double, int>(tp_d.at(j), tp_i.at(j)));
                }
              for (size_t j = 0; j < fp_d.size(); j++)
                {
                  fp.push_back(std::pair<double, int>(fp_d.at(j), fp_i.at(j)));
                }

              if (tp.size() > 0 or fp.size() > 0 or num_pos > 0)
                {
                  APs_count_all += 1;
                  double local_ap = compute_ap(tp, fp, num_pos);
                  if (APs.find(label) == APs.end())
                    {
                      APs[label] = local_ap;
                      APs_count[label] = 1;
                    }
                  else
                    {
                      APs[label] += local_ap;
                      APs_count[label] += 1;
                    }
                  mmAP += local_ap;
                }
              else if (APs.find(label) == APs.end())
                {
                  APs[label] = 0.0;
                  APs_count[label] = 0;
                }
            }
        }
      for (auto ap : APs)
        {
          if (APs_count[ap.first] == 0)
            APs[ap.first] = 0.0;
          else
            APs[ap.first] /= static_cast<float>(APs_count[ap.first]);
        }
      if (APs_count_all == 0)
        return 0.0;
      return mmAP / static_cast<double>(APs_count_all);
    }

    // measure: AUC
    static double auc(const APIData &ad)
    {
      std::vector<double> pred1;
      std::vector<double> targets;
      int batch_size = ad.get("batch_size").get<int>();
      for (int i = 0; i < batch_size; i++)
        {
          APIData bad = ad.getobj(std::to_string(i));
          pred1.push_back(bad.get("pred").get<std::vector<double>>().at(1));
          targets.push_back(bad.get("target").get<double>());
        }
      return auc(pred1, targets);
    }
    static double auc(const std::vector<double> &pred,
                      const std::vector<double> &targets)
    {
      class PredictionAndAnswer
      {
      public:
        PredictionAndAnswer(const float &f, const int &i)
            : prediction(f), answer(i)
        {
        }
        ~PredictionAndAnswer()
        {
        }
        float prediction;
        int answer; // this is either 0 or 1
      };
      std::vector<PredictionAndAnswer> p;
      for (size_t i = 0; i < pred.size(); i++)
        p.emplace_back(pred.at(i), targets.at(i));
      int count = p.size();

      std::sort(
          p.begin(), p.end(),
          [](const PredictionAndAnswer &p1, const PredictionAndAnswer &p2) {
            return p1.prediction < p2.prediction;
          });

      int i, truePos, tp0, accum, tn, ones = 0;
      float threshold; // predictions <= threshold are classified as zeros

      for (i = 0; i < count; i++)
        ones += p[i].answer;
      if (0 == ones || count == ones)
        return 1;

      truePos = tp0 = ones;
      accum = tn = 0;
      threshold = p[0].prediction;
      for (i = 0; i < count; i++)
        {
          if (p[i].prediction != threshold)
            { // threshold changes
              threshold = p[i].prediction;
              accum += tn * (truePos + tp0); // 2* the area of trapezoid
              tp0 = truePos;
              tn = 0;
            }
          tn += 1 - p[i].answer; // x-distance between adjacent points
          truePos -= p[i].answer;
        }
      accum += tn * (truePos + tp0); // 2* the area of trapezoid
      return accum / static_cast<double>((2 * ones * (count - ones)));
    }

    // measure: multiclass logarithmic loss
    static double mcll(const APIData &ad)
    {
      double ll = 0.0;
      int batch_size = ad.get("batch_size").get<int>();
      for (int i = 0; i < batch_size; i++)
        {
          APIData bad = ad.getobj(std::to_string(i));
          std::vector<double> predictions
              = bad.get("pred").get<std::vector<double>>();
          double target = bad.get("target").get<double>();
          ll -= std::log(predictions.at(target));
        }
      return ll / static_cast<double>(batch_size);
    }

    // measure: Mathew correlation coefficient for binary classes
    static double mcc(const APIData &ad)
    {
      int nclasses = ad.get("nclasses").get<int>();
      dMat conf_matrix = dMat::Zero(nclasses, nclasses);
      int batch_size = ad.get("batch_size").get<int>();
      for (int i = 0; i < batch_size; i++)
        {
          APIData bad = ad.getobj(std::to_string(i));
          std::vector<double> predictions
              = bad.get("pred").get<std::vector<double>>();
          int maxpr = std::distance(
              predictions.begin(),
              std::max_element(predictions.begin(), predictions.end()));
          double target = bad.get("target").get<double>();
          if (target < 0)
            throw OutputConnectorBadParamException(
                "negative supervised discrete target (e.g. wrong use of "
                "label_offset ?");
          else if (target >= nclasses)
            throw OutputConnectorBadParamException(
                "target class has id " + std::to_string(target)
                + " is higher than the number of classes "
                + std::to_string(nclasses)
                + " (e.g. wrong number of classes specified with nclasses");
          conf_matrix(maxpr, static_cast<int>(target)) += 1.0;
        }
      double tp = conf_matrix(0, 0);
      double tn = conf_matrix(1, 1);
      double fn = conf_matrix(0, 1);
      double fp = conf_matrix(1, 0);
      double den = (tp + fp) * (tp + fn) * (tn + fp) * (tn + fn);
      if (den == 0.0)
        den = 1.0;
      double mcc = (tp * tn - fp * fn) / std::sqrt(den);
      return mcc;
    }

    static std::tuple<double, std::vector<double>>
    distl(const APIData &ad, float thres, bool compute_all_distl,
          bool l1 = false)
    {
      double eucl = 0.0;
      unsigned int psize = ad.getobj(std::to_string(0))
                               .get("pred")
                               .get<std::vector<double>>()
                               .size();
      std::vector<double> all_eucl;
      if (compute_all_distl)
        all_eucl.resize(psize, 0.0);
      int batch_size = ad.get("batch_size").get<int>();
      bool has_ignore = ad.has("ignore_label");

      int ignore_label = -10000;
      if (has_ignore)
        ignore_label = ad.get("ignore_label").get<int>();

      for (int i = 0; i < batch_size; i++)
        {
          APIData bad = ad.getobj(std::to_string(i));
          std::vector<double> predictions
              = bad.get("pred").get<std::vector<double>>();
          std::vector<double> target;
          if (predictions.size() > 1)
            target = bad.get("target").get<std::vector<double>>();
          else
            target.push_back(bad.get("target").get<double>());
          int reg_dim = predictions.size();
          double leucl = 0;
          for (size_t j = 0; j < target.size(); j++)
            {
              int t = target.at(j);
              if (has_ignore && t - static_cast<double>(ignore_label) < 1E-9)
                continue;
              double diff = fabs(predictions.at(j) - target.at(j));
              if (thres >= 0)
                {
                  if (diff >= thres)
                    {
                      if (l1)
                        eucl += diff / reg_dim;
                      else
                        leucl += diff * diff;
                      if (compute_all_distl)
                        {
                          if (l1)
                            all_eucl[j] += diff;
                          else
                            all_eucl[j] += diff * diff;
                        }
                    }
                }
              else
                {
                  if (l1)
                    eucl += diff / reg_dim;
                  else
                    leucl += diff * diff;
                  if (compute_all_distl)
                    {
                      if (l1)
                        all_eucl[j] += diff;
                      else
                        all_eucl[j] += diff * diff;
                    }
                }
            }
          if (!l1)
            {
              eucl += sqrt(leucl) / reg_dim;
              if (compute_all_distl)
                for (size_t j = 0; j < target.size(); ++j)
                  all_eucl[j] = sqrt(all_eucl[j]);
            }
        }

      if (compute_all_distl)
        for (unsigned int i = 0; i < all_eucl.size(); ++i)
          all_eucl[i] /= static_cast<double>(batch_size);

      return std::make_tuple(eucl / static_cast<double>(batch_size), all_eucl);
    }

    static std::tuple<double, std::vector<double>>
    percentl(const APIData &ad, bool compute_all_distl)
    {
      double percent = 0.0;
      unsigned int psize = ad.getobj(std::to_string(0))
                               .get("pred")
                               .get<std::vector<double>>()
                               .size();
      std::vector<double> all_percent;
      if (compute_all_distl)
        all_percent.resize(psize, 0.0);
      int batch_size = ad.get("batch_size").get<int>();
      bool has_ignore = ad.has("ignore_label");

      int ignore_label = -10000;
      if (has_ignore)
        ignore_label = ad.get("ignore_label").get<int>();

      for (int i = 0; i < batch_size; i++)
        {
          APIData bad = ad.getobj(std::to_string(i));
          std::vector<double> predictions
              = bad.get("pred").get<std::vector<double>>();
          std::vector<double> target;
          if (predictions.size() > 1)
            target = bad.get("target").get<std::vector<double>>();
          else
            target.push_back(bad.get("target").get<double>());
          int reg_dim = predictions.size();
          for (size_t j = 0; j < target.size(); j++)
            {
              int t = target.at(j);
              if (has_ignore && t - static_cast<double>(ignore_label) < 1E-9)
                continue;
              double reldiff = fabs((predictions.at(j) - target.at(j)))
                               / (fabs(target.at(j)) + 1E-9);
              percent += reldiff / reg_dim;
              if (compute_all_distl)
                all_percent[j] += reldiff;
            }
        }

      if (compute_all_distl)
        for (unsigned int i = 0; i < all_percent.size(); ++i)
          {
            all_percent[i] /= static_cast<double>(batch_size);
            all_percent[i] *= 100.0;
          }

      return std::make_tuple(percent * 100.0 / static_cast<double>(batch_size),
                             all_percent);
    }

    // measure: gini coefficient
    static double comp_gini(const std::vector<double> &a,
                            const std::vector<double> &p)
    {
      struct K
      {
        double a, p;
      };
      std::vector<K> k(a.size());
      for (size_t i = 0; i != a.size(); ++i)
        k[i] = { a[i], p[i] };
      std::stable_sort(k.begin(), k.end(),
                       [](const K &a, const K &b) { return a.p > b.p; });
      double accPopPercSum = 0, accLossPercSum = 0, giniSum = 0, sum = 0;
      for (auto &i : a)
        sum += i;
      for (auto &i : k)
        {
          accLossPercSum += i.a / sum;
          accPopPercSum += 1.0 / a.size();
          giniSum += accLossPercSum - accPopPercSum;
        }
      return giniSum / a.size();
    }
    static double comp_gini_normalized(const std::vector<double> &a,
                                       const std::vector<double> &p)
    {
      return comp_gini(a, p) / comp_gini(a, a);
    }

    static double gini(const APIData &ad, const bool &regression)
    {
      int batch_size = ad.get("batch_size").get<int>();
      std::vector<double> a(batch_size);
      std::vector<double> p(batch_size);
      for (int i = 0; i < batch_size; i++)
        {
          APIData bad = ad.getobj(std::to_string(i));
          a.at(i) = bad.get("target").get<double>();
          if (regression)
            p.at(i) = bad.get("pred").get<std::vector<double>>().at(
                0); // XXX: could be vector for multi-dimensional regression
                    // ->
                    // TODO: in supervised mode, get best pred index ?
          else
            {
              std::vector<double> allpreds
                  = bad.get("pred").get<std::vector<double>>();
              a.at(i) = std::distance(
                  allpreds.begin(),
                  std::max_element(allpreds.begin(), allpreds.end()));
            }
        }
      return comp_gini_normalized(a, p);
    }

    // for debugging purposes.
    /**
     * \brief print supervised output to string
     */
    void to_str(std::string &out, const int &rmax) const
    {
      auto vit = _vcats.begin();
      while (vit != _vcats.end())
        {
          int count = 0;
          out += "-------------\n";
          out += (*vit).first + "\n";
          auto mit = _vvcats.at((*vit).second)._cats.begin();
          while (mit != _vvcats.at((*vit).second)._cats.end() && count < rmax)
            {
              out += "accuracy=" + std::to_string((*mit).first)
                     + " -- cat=" + (*mit).second + "\n";
              ++mit;
              ++count;
            }
          ++vit;
        }
    }

    /**
     * \brief write supervised output object to data object
     * @param out data destination
     * @param regression whether a regression task
     * @param autoencoder whether an autoencoder architecture
     * @param has_bbox whether an object detection task
     * @param has_roi whether using feature extraction and object detection
     * @param has_mask whether a mask creation task
     * @param indexed_uris list of indexed uris, if any
     */
    void to_ad(APIData &out, const bool &regression, const bool &autoencoder,
               const bool &has_bbox, const bool &has_roi, const bool &has_mask,
               const bool &timeseries,
               const std::unordered_set<std::string> &indexed_uris) const
    {
#ifndef USE_SIMSEARCH
      (void)indexed_uris;
#endif
      static std::string cl = "classes";
      static std::string ve = "vector";
      static std::string ae = "losses";
      static std::string bb = "bbox";
      static std::string roi = "vals";
      static std::string rois = "rois";
      static std::string series = "series";
      static std::string mask = "mask";
      static std::string phead = "prob";
      static std::string chead = "cat";
      static std::string vhead = "val";
      static std::string ahead = "loss";
      static std::string last = "last";
      std::unordered_set<std::string>::const_iterator hit;
      std::vector<APIData> vpred;
      for (size_t i = 0; i < _vvcats.size(); i++)
        {
          APIData adpred;
          std::vector<APIData> v;
          auto bit = _vvcats.at(i)._bboxes.begin();
          auto vit = _vvcats.at(i)._vals.begin();
          auto mit = _vvcats.at(i)._cats.begin();
          auto maskit = _vvcats.at(i)._masks.begin();
          while (mit != _vvcats.at(i)._cats.end())
            {
              APIData nad;
              if (!autoencoder)
                nad.add(chead, (*mit).second);
              if (regression)
                nad.add(vhead, (*mit).first);
              else if (autoencoder)
                nad.add(ahead, (*mit).first);
              else
                nad.add(phead, (*mit).first);
              if (has_bbox || has_roi || has_mask)
                {
                  nad.add(bb, (*bit).second);
                  ++bit;
                }
              if (has_roi)
                {
                  /* std::vector<std::string> keys =
                   * (*vit).second.list_keys();
                   */
                  /* std::copy(keys.begin(), keys.end(),
                   * std::ostream_iterator<std::string>(std::cout, "'")); */
                  /* std::cout << std::endl; */
                  nad.add(
                      roi,
                      (*vit).second.get("vals").get<std::vector<double>>());
                  ++vit;
                }
              if (has_mask)
                {
                  nad.add(mask, (*maskit).second);
                  ++maskit;
                }
              ++mit;
              if (mit == _vvcats.at(i)._cats.end())
                nad.add(last, true);
              v.push_back(nad);
            }
          if (_vvcats.at(i)._loss
              > 0.0) // XXX: not set by Caffe in prediction mode for now
            adpred.add("loss", _vvcats.at(i)._loss);
          adpred.add("uri", _vvcats.at(i)._label);
#ifdef USE_SIMSEARCH
          if (!_vvcats.at(i)._index_uri.empty())
            adpred.add("index_uri", _vvcats.at(i)._index_uri);
          if (!indexed_uris.empty()
              && (hit = indexed_uris.find(_vvcats.at(i)._label))
                     != indexed_uris.end())
            adpred.add("indexed", true);
          if (!_vvcats.at(i)._nns.empty() || !_vvcats.at(i)._bbox_nns.empty())
            {
              if (!has_roi)
                {
                  std::vector<APIData> ad_nns;
                  auto nnit = _vvcats.at(i)._nns.begin();
                  while (nnit != _vvcats.at(i)._nns.end())
                    {
                      APIData ad_nn;
                      ad_nn.add("uri", (*nnit).second._uri);
                      ad_nn.add("dist", (*nnit).first);
                      ad_nns.push_back(ad_nn);
                      ++nnit;
                    }
                  adpred.add("nns", ad_nns);
                }
              else // has_roi
                {
                  for (size_t bb = 0; bb < _vvcats.at(i)._bbox_nns.size();
                       bb++)
                    {
                      std::vector<APIData> ad_nns;
                      auto nnit = _vvcats.at(i)._bbox_nns.at(bb).begin();
                      while (nnit != _vvcats.at(i)._bbox_nns.at(bb).end())
                        {
                          APIData ad_nn;
                          ad_nn.add("uri", (*nnit).second._uri);
                          ad_nn.add("dist", (*nnit).first);
                          ad_nn.add("prob", (*nnit).second._prob);
                          ad_nn.add("cat", (*nnit).second._cat);
                          APIData ad_bbox;
                          ad_bbox.add("xmin", (*nnit).second._bbox.at(0));
                          ad_bbox.add("ymin", (*nnit).second._bbox.at(1));
                          ad_bbox.add("xmax", (*nnit).second._bbox.at(2));
                          ad_bbox.add("ymax", (*nnit).second._bbox.at(3));
                          ad_nn.add("bbox", ad_bbox);
                          ad_nns.push_back(ad_nn);
                          ++nnit;
                        }
                      v.at(bb).add("nns",
                                   ad_nns); // v is in roi object vector
                    }
                }
            }
#endif
          if (!_vvcats.at(i)._series.empty())
            {
              auto sit = _vvcats.at(i)._series.begin();
              while (sit != _vvcats.at(i)._series.end())
                {
                  APIData nad;
                  nad.add("out",
                          (*sit).second.get("out").get<std::vector<double>>());
                  ++sit;
                  if (sit == _vvcats.at(i)._series.end())
                    nad.add(last, true);
                  v.push_back(nad);
                }
            }

          if (timeseries)
            adpred.add(series, v);
          else if (regression)
            adpred.add(ve, v);
          else if (autoencoder)
            adpred.add(ae, v);
          else if (has_roi)
            adpred.add(rois, v);
          else
            adpred.add(cl, v);
          vpred.push_back(adpred);
        }
      out.add("predictions", vpred);
    }

    std::unordered_map<std::string, int>
        _vcats;                      /**< batch of results, per uri. */
    std::vector<sup_result> _vvcats; /**< ordered results, per uri. */

    // options
    int _best = 1;
#ifdef USE_SIMSEARCH
    int _search_nn = 10; /**< default nearest neighbors per search. */
#endif
  };
}

#endif
