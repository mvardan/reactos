<?xml version="1.0" encoding="US-ASCII"?>
<!--This file was created automatically by html2xhtml-->
<!--from the HTML stylesheets. Do not edit this file.-->
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:doc="http://nwalsh.com/xsl/documentation/1.0" version="1.0" exclude-result-prefixes="doc">

<xsl:import href="chunk.xsl"/>

<!-- Ok, using the onechunk parameter makes this all work again. -->
<!-- It does have the disadvantage that it only works for documents that have -->
<!-- a root element that is considered a chunk by the chunk.xsl stylesheet. -->
<!-- Ideally, onechunk would let anything be a chunk. But not today. -->

<xsl:param name="onechunk" select="1"/>

<xsl:template name="href.target.uri">
  <xsl:param name="object" select="."/>
  <xsl:text>#</xsl:text>
  <xsl:call-template name="object.id">
    <xsl:with-param name="object" select="$object"/>
  </xsl:call-template>
</xsl:template>

</xsl:stylesheet>
