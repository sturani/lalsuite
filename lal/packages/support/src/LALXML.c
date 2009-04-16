/*
 *  Copyright (C) 2007 Jolien Creighton
 *  Copyright (C) 2009 Oliver Bock
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with with program; see the file COPYING. If not, write to the
 *  Free Software Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 *  MA  02111-1307  USA
 */

#include <stdio.h>
#include <string.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <lal/LALXML.h>
#include <lal/XLALError.h>
#include <lal/LALDatatypes.h>
#include <lal/LALStdio.h>

#define INT4STR_MAXLEN 15
#define XPATHSTR_MAXLEN 150

static void print_element_names(xmlNode *node)
{
    xmlNode *cur;
    for (cur = node; cur; cur = cur->next) {
        if (cur->type == XML_ELEMENT_NODE)
            printf("node type: Element, name: %s\n", cur->name);
        print_element_names(cur->children);
    }
    return;
}

int XLALXMLFilePrintElements(const char *fname)
{
    static const char file[] = "XLALXMLFilePrintElements";
    xmlDoc  *doc;

    /* make sure that the shared library is the same as the
     * library version the code was compiled against */
    LIBXML_TEST_VERSION

    if (!(doc = xmlReadFile(fname, NULL, 0)))
        XLAL_ERROR(file, XLAL_EIO);
    print_element_names(xmlDocGetRootElement(doc));
    xmlFreeDoc(doc);
    xmlCleanupParser(); /* free global variables in parser */
    return 0;
}


/**
 * \brief Takes a XML fragment (tree) and turns it into a VOTable document
 *
 * This function wraps a given XML fragment in a VOTABLE element to turn it into
 * a valid document. Please make sure that the root element of the given fragment
 * is a valid child of the VOTABLE element (VOTable schema 1.1):
 * \li DESCRIPTION
 * \li COOSYS
 * \li PARAM
 * \li INFO
 * \li RESOURCE
 *
 * \param xmlTree The XML fragment to be turned into a VOTable document
 *
 * \return A pointer to a xmlDoc that represents the full VOTable XML document.
 * In case of an error, a null-pointer is returned.\n
 * \b Important: the caller is responsible to free the allocated memory (when the
 * document isn't needed anymore) using xmlFreeDoc().
 *
 * \sa XLALLIGOTimeGPS2VOTableNode
 *
 * \author Oliver Bock\n
 * Albert-Einstein-Institute Hannover, Germany
 */
xmlDocPtr XLALCreateVOTableXMLFromTree(const xmlNodePtr xmlTree)
{
    /* set up local variables */
    static const CHAR *logReference = "XLALCreateVOTableXMLFromTree";
    xmlDocPtr xmlDocument = NULL;
    xmlNodePtr xmlRootNode = NULL;

    /* make sure that the shared library is the same as the
     * library version the code was compiled against */
    LIBXML_TEST_VERSION

    /* sanity check */
    if(!xmlTree) {
        XLALPrintError("Invalid input parameter: xmlTree\n");
        XLAL_ERROR_NULL(logReference, XLAL_EINVAL);
    }

    /* set up XML document */
    xmlDocument = xmlNewDoc(BAD_CAST("1.0"));
    if(xmlDocument == NULL) {
        XLALPrintError("VOTable document instantiation failed\n");
        XLAL_ERROR_NULL(logReference, XLAL_EFAILED);
    }

    /* set up root node */
    xmlRootNode = xmlNewNode(NULL, BAD_CAST("VOTABLE"));
    if(xmlRootNode == NULL) {
        /* clean up */
        xmlFreeDoc(xmlDocument);

        XLALPrintError("VOTable root element instantiation failed\n");
        XLAL_ERROR_NULL(logReference, XLAL_EFAILED);
    }

    xmlDocSetRootElement(xmlDocument, xmlRootNode);

    /* append tree to root node */
    if(!xmlAddChild(xmlRootNode, xmlTree)) {
        /* clean up */
        xmlFreeNode(xmlRootNode);
        xmlFreeDoc(xmlDocument);

        XLALPrintError("Couldn't append given tree to VOTable root element\n");
        XLAL_ERROR_NULL(logReference, XLAL_EFAILED);
    }

    /* return VOTable document (needs to be xmlFreeDoc'd by caller!!!) */
    return xmlDocument;
}


/**
 * \brief Performs a XPath search on a XML document to retrieve the content of a single node
 *
 * This function searches the given XML document using the given XPath statement.
 * The XPath statement \b must be specified in such a way that at most a single node
 * will be found.
 *
 * \param xmlDocument The XML document to be searched
 * \param xpath The XPath statement to be used to search the given XML document
 *
 * \return A pointer to a xmlChar that holds the content (string) of the node
 * specified by the given XPath statement. The content will be encoded in UTF-8.
 * In case of an error, a null-pointer is returned.\n
 * \b Important: the caller is responsible to free the allocated memory (when the
 * string isn't needed anymore) using xmlFree().
 *
 * \sa XLALVOTableXML2LIGOTimeGPSByName
 *
 * \author Oliver Bock\n
 * Albert-Einstein-Institute Hannover, Germany
 */
xmlChar * XLALGetSingleNodeContentByXPath(const xmlDocPtr xmlDocument, const char *xpath)
{
    /* set up local variables */
    static const CHAR *logReference = "XLALGetSingleNodeContentByXPath";
    xmlXPathContextPtr xpathCtx = NULL;
    xmlChar *xpathExpr = NULL;
    xmlXPathObjectPtr xpathObj = NULL;
    xmlNodeSetPtr xmlNodes = NULL;
    xmlChar *nodeContent = NULL;
    INT4 nodeCount;

    /* sanity checks */
    if(!xmlDocument) {
        XLALPrintError("Invalid input parameter: xmlDocument\n");
        XLAL_ERROR_NULL(logReference, XLAL_EINVAL);
    }
    if(!xpath || strlen(xpath) <= 0) {
        XLALPrintError("Invalid input parameter: xpath\n");
        XLAL_ERROR_NULL(logReference, XLAL_EINVAL);
    }

    /* prepare xpath context */
    xpathCtx = xmlXPathNewContext(xmlDocument);
    if(xpathCtx == NULL) {
        XLALPrintError("XPATH context instantiation failed\n");
        XLAL_ERROR_NULL(logReference, XLAL_EFAILED);
    }

    /* prepare xpath expression */
    xpathExpr = xmlCharStrdup(xpath);
    if(xpathExpr == NULL) {
        /* clean up */
        xmlXPathFreeContext(xpathCtx);

        XLALPrintError("XPATH statement preparation failed\n");
        XLAL_ERROR_NULL(logReference, XLAL_EFAILED);
    }

    /* run xpath query */
    xpathObj = xmlXPathEvalExpression(xpathExpr, xpathCtx);
    if(xpathObj == NULL) {
        /* clean up */
        xmlFree(xpathExpr);
        xmlXPathFreeContext(xpathCtx);

        XLALPrintError("XPATH evaluation failed\n");
        XLAL_ERROR_NULL(logReference, XLAL_EFAILED);
    }

    /* retrieve node set returned by xpath query */
    xmlNodes = xpathObj->nodesetval;

    /* how many nodes did we find? */
    nodeCount = (xmlNodes) ? xmlNodes->nodeNr : 0;
    if(nodeCount <= 0) {
        /* clean up */
        xmlXPathFreeObject(xpathObj);
        xmlFree(xpathExpr);
        xmlXPathFreeContext(xpathCtx);

        XLALPrintError("XPATH search didn't return any nodes\n");
        XLAL_ERROR_NULL(logReference, XLAL_EDOM);
    }
    else if(nodeCount > 1) {
        /* clean up */
        xmlXPathFreeObject(xpathObj);
        xmlFree(xpathExpr);
        xmlXPathFreeContext(xpathCtx);

        XLALPrintError("XPATH search did return %i nodes where only 1 was expected\n", nodeCount);
        XLAL_ERROR_NULL(logReference, XLAL_EDOM);
    }
    else {
        nodeContent = xmlNodeListGetString(xmlDocument, xmlNodes->nodeTab[0]->xmlChildrenNode, 1);
    }

    /* clean up */
    xmlXPathFreeObject(xpathObj);
    xmlFree(xpathExpr);
    xmlXPathFreeContext(xpathCtx);

    /* return node content (needs to be xmlFree'd by caller!!!) */
    return nodeContent;
}


/**
 * \brief Serializes a LIGOTimeGPS structure into a VOTable XML node
 *
 * This function takes a LIGOTimeGPS structure and serializes it into a VOTable
 * RESOURCE node identified by the given name. The returned xmlNode can then be
 * embedded into an existing node hierarchy or turned into a full VOTable document.
 *
 * \param ltg Pointer to the LIGOTimeGPS structure to be serialized
 * \param name Unique identifier of this particular LIGOTimeGPS structure instance
 *
 * \return A pointer to a xmlNode that holds the VOTable fragment that represents
 * the LIGOTimeGPS structure.
 * In case of an error, a null-pointer is returned.\n
 * \b Important: the caller is responsible to free the allocated memory (when the
 * fragment isn't needed anymore) using xmlFreeNode(). Alternatively, xmlFreeDoc()
 * can be used later on when the returned fragment has been embedded in a XML document.
 *
 * \sa XLALCreateVOTableXMLFromTree
 *
 * \author Oliver Bock\n
 * Albert-Einstein-Institute Hannover, Germany
 */
xmlNodePtr XLALLIGOTimeGPS2VOTableNode(const LIGOTimeGPS *const ltg, const char *name)
{
    /* set up local variables */
    static const CHAR *logReference = "XLALLIGOTimeGPS2VOTableNode";
    CHAR gpsSecondsBuffer[INT4STR_MAXLEN] = {0};
    CHAR gpsNanoSecondsBuffer[INT4STR_MAXLEN] = {0};
    xmlNodePtr xmlResourceNode = NULL;
    xmlNodePtr xmlParamNodeGpsSeconds = NULL;
    xmlNodePtr xmlParamNodeGpsNanoSeconds = NULL;

    /* make sure that the shared library is the same as the
     * library version the code was compiled against */
    LIBXML_TEST_VERSION

    /* check and prepare input parameters */
    if(!ltg || LALSnprintf(gpsSecondsBuffer, INT4STR_MAXLEN, "%i", ltg->gpsSeconds) < 0) {
        XLALPrintError("Invalid input parameter: LIGOTimeGPS->gpsSeconds\n");
        XLAL_ERROR_NULL(logReference, XLAL_EINVAL);
    }
    if(!ltg || LALSnprintf(gpsNanoSecondsBuffer, INT4STR_MAXLEN, "%i", ltg->gpsNanoSeconds) < 0) {
        XLALPrintError("Invalid input parameter: LIGOTimeGPS->gpsNanoSeconds\n");
        XLAL_ERROR_NULL(logReference, XLAL_EINVAL);
    }
    if(!name || strlen(name) <= 0) {
        XLALPrintError("Invalid input parameter: name\n");
        XLAL_ERROR_NULL(logReference, XLAL_EINVAL);
    }

    /* set up RESOURCE node*/
    xmlResourceNode = xmlNewNode(NULL, BAD_CAST("RESOURCE"));
    if(xmlResourceNode == NULL) {
        XLALPrintError("Element instantiation failed: RESOURCE\n");
        XLAL_ERROR_NULL(logReference, XLAL_EFAILED);
    }
    if(!xmlNewProp(xmlResourceNode, BAD_CAST("utype"), BAD_CAST("LIGOTimeGPS"))) {
        /* clean up */
        xmlFreeNode(xmlResourceNode);

        XLALPrintError("Attribute instantiation failed: utype\n");
        XLAL_ERROR_NULL(logReference, XLAL_EFAILED);
    }
    if(!xmlNewProp(xmlResourceNode, BAD_CAST("name"), BAD_CAST(name))) {
        /* clean up */
        xmlFreeNode(xmlResourceNode);

        XLALPrintError("Attribute instantiation failed: name\n");
        XLAL_ERROR_NULL(logReference, XLAL_EFAILED);
    }

    /* set up RESOURCE node child (first PARAM) */
    xmlParamNodeGpsSeconds = xmlNewChild(xmlResourceNode, NULL, BAD_CAST("PARAM"), NULL);
    if(xmlParamNodeGpsSeconds == NULL) {
        /* clean up */
        xmlFreeNode(xmlResourceNode);

        XLALPrintError("Element instantiation failed: PARAM\n");
        XLAL_ERROR_NULL(logReference, XLAL_EFAILED);
    }
    if(!xmlNewProp(xmlParamNodeGpsSeconds, BAD_CAST("name"), BAD_CAST("gpsSeconds"))) {
        /* clean up */
        xmlFreeNode(xmlResourceNode);

        XLALPrintError("Attribute instantiation failed: name\n");
        XLAL_ERROR_NULL(logReference, XLAL_EFAILED);
    }
    if(!xmlNewProp(xmlParamNodeGpsSeconds, BAD_CAST("datatype"), BAD_CAST("int"))) {
        /* clean up */
        xmlFreeNode(xmlResourceNode);

        XLALPrintError("Attribute instantiation failed: datatype\n");
        XLAL_ERROR_NULL(logReference, XLAL_EFAILED);
    }
    if(!xmlNewProp(xmlParamNodeGpsSeconds, BAD_CAST("unit"), BAD_CAST("s"))) {
        /* clean up */
        xmlFreeNode(xmlResourceNode);

        XLALPrintError("Attribute instantiation failed: unit\n");
        XLAL_ERROR_NULL(logReference, XLAL_EFAILED);
    }
    if(!xmlNewProp(xmlParamNodeGpsSeconds, BAD_CAST("value"), BAD_CAST(gpsSecondsBuffer))) {
        /* clean up */
        xmlFreeNode(xmlResourceNode);

        XLALPrintError("Attribute instantiation failed: value\n");
        XLAL_ERROR_NULL(logReference, XLAL_EFAILED);
    }

    /* set up RESOURCE node child (second PARAM) */
    xmlParamNodeGpsNanoSeconds = xmlNewChild(xmlResourceNode, NULL, BAD_CAST("PARAM"), NULL);
    if(xmlParamNodeGpsNanoSeconds == NULL) {
        /* clean up */
        xmlFreeNode(xmlResourceNode);

        XLALPrintError("Element instantiation failed: PARAM\n");
        XLAL_ERROR_NULL(logReference, XLAL_EFAILED);
    }
    if(!xmlNewProp(xmlParamNodeGpsNanoSeconds, BAD_CAST("name"), BAD_CAST("gpsNanoSeconds"))) {
        /* clean up */
        xmlFreeNode(xmlResourceNode);

        XLALPrintError("Attribute instantiation failed: name\n");
        XLAL_ERROR_NULL(logReference, XLAL_EFAILED);
    }
    if(!xmlNewProp(xmlParamNodeGpsNanoSeconds, BAD_CAST("datatype"), BAD_CAST("int"))) {
        /* clean up */
        xmlFreeNode(xmlResourceNode);

        XLALPrintError("Attribute instantiation failed: datatype\n");
        XLAL_ERROR_NULL(logReference, XLAL_EFAILED);
    }
    if(!xmlNewProp(xmlParamNodeGpsNanoSeconds, BAD_CAST("unit"), BAD_CAST("ns"))) {
        /* clean up */
        xmlFreeNode(xmlResourceNode);

        XLALPrintError("Attribute instantiation failed: unit\n");
        XLAL_ERROR_NULL(logReference, XLAL_EFAILED);
    }
    if(!xmlNewProp(xmlParamNodeGpsNanoSeconds, BAD_CAST("value"), BAD_CAST(gpsNanoSecondsBuffer))) {
        /* clean up */
        xmlFreeNode(xmlResourceNode);

        XLALPrintError("Attribute instantiation failed: value\n");
        XLAL_ERROR_NULL(logReference, XLAL_EFAILED);
    }

    /* return RESOURCE node (needs to be xmlFreeNode'd or xmlFreeDoc'd by caller!!!) */
    return xmlResourceNode;
}


/**
 * \brief Serializes a LIGOTimeGPS structure into a VOTable XML string
 *
 * This function takes a LIGOTimeGPS structure and serializes it into a full-fledged
 * VOTable XML string containing the serialized structure as the only child element.\n
 * Essentially, this function is just a wrapper for XLALLIGOTimeGPS2VOTableNode and
 * XLALCreateVOTableXMLFromTree followed by a dump of the VOTable document into a
 * string.\n
 *
 * \param ltg Pointer to the LIGOTimeGPS structure to be serialized
 * \param name Unique identifier of this particular LIGOTimeGPS structure instance
 *
 * \return A pointer to a xmlChar (string) that holds the VOTable document containing
 * solely the LIGOTimeGPS structure. Please note that the string will be encoded in UTF-8.
 * In case of an error, a null-pointer is returned.\n
 * \b Important: the caller is responsible to free the allocated memory (when the
 * string isn't needed anymore) using xmlFree().
 *
 * \sa XLALLIGOTimeGPS2VOTableNode
 * \sa XLALCreateVOTableXMLFromTree
 *
 * \author Oliver Bock\n
 * Albert-Einstein-Institute Hannover, Germany
 */
xmlChar * XLALLIGOTimeGPS2VOTableXML(const LIGOTimeGPS *const ltg, const char *name)
{
    /* set up local variables */
    static const CHAR *logReference = "XLALLIGOTimeGPS2VOTableXML";
    xmlChar *xmlStringBuffer = NULL;
    INT4 xmlStringBufferSize = -1;
    xmlNodePtr xmlTree;
    xmlDocPtr xmlDocument;

    /* sanity checks */
    if(!ltg) {
        XLALPrintError("Invalid input parameter: ltg\n");
        XLAL_ERROR_NULL(logReference, XLAL_EINVAL);
    }
    if(!name || strlen(name) <= 0) {
        XLALPrintError("Invalid input parameter: name\n");
        XLAL_ERROR_NULL(logReference, XLAL_EINVAL);
    }

    /* prepare XML serialization */
    xmlThrDefIndentTreeOutput(1);

    /* build VOTable fragment (tree) */
    xmlTree = XLALLIGOTimeGPS2VOTableNode(ltg, name);
    if(xmlTree == NULL) {
        XLALPrintError("VOTable fragment construction failed\n");
        XLAL_ERROR_NULL(logReference, XLAL_EFAILED);
    }

    /* build VOTable document */
    xmlDocument = XLALCreateVOTableXMLFromTree(xmlTree);
    if(xmlDocument == NULL) {
        /* clean up */
        xmlFreeNode(xmlTree);
        xmlCleanupParser();

        XLALPrintError("VOTable document construction failed\n");
        XLAL_ERROR_NULL(logReference, XLAL_EFAILED);
    }

    /* dump VOTable document to formatted XML string */
    xmlDocDumpFormatMemoryEnc(xmlDocument, &xmlStringBuffer, &xmlStringBufferSize, "UTF-8", 1);
    if(xmlStringBufferSize <= 0) {
        /* clean up */
        xmlFreeDoc(xmlDocument);
        xmlCleanupParser();

        XLALPrintError("VOTable document dump failed\n");
        XLAL_ERROR_NULL(logReference, XLAL_EFAILED);
    }

    /* clean up */
    xmlFreeDoc(xmlDocument);
    xmlCleanupParser();

    /* return XML string (needs to be xmlFree'd by caller!!!) */
    return xmlStringBuffer;
}


/**
 * \brief Deserializes a LIGOTimeGPS structure from a VOTable XML string
 *
 * This function takes a VOTable XML document (string) and deserializes (extracts)
 * the LIGOTimeGPS structure identified by the given name.
 *
 * \param xml Pointer to the VOTable XML document (string) containing the structure
 * \param name Unique identifier of the particular LIGOTimeGPS structure to be deserialized
 * \param ltg Pointer to an empty LIGOTimeGPS structure to store the deserialized instance
 *
 * \return XLAL_SUCCESS if the specified LIGOTimeGPS structure could be found and
 * deserialized successfully.
 *
 * \sa XLALGetSingleNodeContentByXPath
 *
 * \author Oliver Bock\n
 * Albert-Einstein-Institute Hannover, Germany
 */
INT4 XLALVOTableXML2LIGOTimeGPSByName(const char *xml, const char *name, LIGOTimeGPS *ltg)
{
    /* set up local variables */
    static const CHAR *logReference = "XLALVOTableXML2LIGOTimeGPSByName";
    xmlDocPtr xmlDocument = NULL;
    xmlChar *nodeContent = NULL;
    CHAR xpath[XPATHSTR_MAXLEN] = {0};

    /* sanity checks */
    if(!xml) {
        XLALPrintError("Invalid input parameter: xml\n");
        XLAL_ERROR(logReference, XLAL_EINVAL);
    }
    if(!name || strlen(name) <= 0) {
        XLALPrintError("Invalid input parameter: name\n");
        XLAL_ERROR(logReference, XLAL_EINVAL);
    }
    if(!ltg) {
        XLALPrintError("Invalid input parameter: ltg\n");
        XLAL_ERROR(logReference, XLAL_EINVAL);
    }

    /* parse XML document */
    xmlDocument = xmlReadMemory(xml, strlen(xml), NULL, "UTF-8", 0);
    if(xmlDocument == NULL) {
        /* clean up */
        xmlCleanupParser();

        XLALPrintError("VOTable document parsing failed\n");
        XLAL_ERROR(logReference, XLAL_EFAILED);
    }

    /* prepare XPATH search for LIGOTimeGPS.gpsSeconds */
    if(LALSnprintf(
            xpath,
            XPATHSTR_MAXLEN,
            "//RESOURCE[@utype='LIGOTimeGPS' and @name='%s']/PARAM[@name='gpsSeconds']/@value",
            name) < 0)
    {
        /* clean up */
        xmlFreeDoc(xmlDocument);
        xmlCleanupParser();

        XLALPrintError("XPATH statement construction failed: LIGOTimeGPS.gpsSeconds\n");
        XLAL_ERROR(logReference, XLAL_EFAILED);
    }

    /* retrieve LIGOTimeGPS.gpsSeconds */
    nodeContent = (xmlChar *) XLALGetSingleNodeContentByXPath(xmlDocument, xpath);

    /* parse and finally store content */
    if(!nodeContent || sscanf((char*)nodeContent, "%i", &ltg->gpsSeconds) == EOF) {
        /* clean up*/
        xmlFree(nodeContent);
        xmlFreeDoc(xmlDocument);
        xmlCleanupParser();

        XLALPrintError("Invalid node content encountered: gpsSeconds\n");
        XLAL_ERROR(logReference, XLAL_EDATA);
    }

    /* prepare XPATH search for LIGOTimeGPS.gpsNanoSeconds */
    if(LALSnprintf(
            xpath,
            XPATHSTR_MAXLEN,
            "//RESOURCE[@utype='LIGOTimeGPS' and @name='%s']/PARAM[@name='gpsNanoSeconds']/@value",
            name) < 0)
    {
        /* clean up */
        xmlFree(nodeContent);
        xmlFreeDoc(xmlDocument);
        xmlCleanupParser();

        XLALPrintError("XPATH statement construction failed: LIGOTimeGPS.gpsNanoSeconds\n");
        XLAL_ERROR(logReference, XLAL_EFAILED);
    }

    /* retrieve LIGOTimeGPS.gpsNanoSeconds */
    nodeContent = (xmlChar *)XLALGetSingleNodeContentByXPath(xmlDocument, xpath);

    /* parse and finally store content */
    if(!nodeContent || sscanf((char*)nodeContent, "%i", &ltg->gpsNanoSeconds) == EOF) {
        /* clean up*/
        xmlFree(nodeContent);
        xmlFreeDoc(xmlDocument);
        xmlCleanupParser();

        XLALPrintError("Invalid node content encountered: gpsNanoSeconds\n");
        XLAL_ERROR(logReference, XLAL_EDATA);
    }

    /* clean up*/
    xmlFree(nodeContent);
    xmlFreeDoc(xmlDocument);
    xmlCleanupParser();

    return XLAL_SUCCESS;
}
